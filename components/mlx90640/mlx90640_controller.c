#include "mlx90640_controller.h"
#include "mlx90640_api.h"
#include "status_led.h"
#include "pcf85063.h"

#include <string.h>
#include <sys/time.h>
#include <strings.h>
#include <stdlib.h>
#include <math.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_console.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#ifndef CONFIG_THERMAL_SENSOR_ONLY_MLX90640
#define SUPPORT_MLX90641 1
#include "MLX90641_API.h"
#else
#define SUPPORT_MLX90641 0
#endif

#ifndef CONFIG_THERMAL_SENSOR_ONLY_MLX90641
#define SUPPORT_MLX90640 1
#include "MLX90640_API.h"
#else
#define SUPPORT_MLX90640 0
#endif

#ifdef CONFIG_THERMAL_SENSOR_DEFAULT_MLX90640
#define DEFAULT_SENSOR_MODEL THERMAL_SENSOR_MLX90640
#else
#define DEFAULT_SENSOR_MODEL THERMAL_SENSOR_MLX90641
#endif

static const char *TAG = "mlx_controller";

static i2c_master_dev_handle_t s_mlx_dev = NULL;
static thermal_sensor_model_t s_model = DEFAULT_SENSOR_MODEL;
#if SUPPORT_MLX90641
static paramsMLX90641 *s_params641 = NULL;
#endif
#if SUPPORT_MLX90640
static paramsMLX90640 *s_params640 = NULL;
#endif
static uint16_t *s_frame_data = NULL;

static bool s_running = false;
static uint8_t s_refresh_rate = 4;
static float s_emissivity = 0.95f;
static uint32_t s_seq = 0;

static thermal_frame_t s_cached_frame;
static bool s_has_frame = false;
static SemaphoreHandle_t s_frame_mutex = NULL;

static SemaphoreHandle_t s_wake_sem = NULL;
static SemaphoreHandle_t s_single_done_sem = NULL;
static bool s_single_requested = false;
static esp_err_t s_single_result = ESP_FAIL;

static mlx_frame_callback_t s_frame_cb = NULL;

static uint8_t hz_to_rate_code(uint8_t hz)
{
    if (hz <= 1) return 1;
    if (hz <= 2) return 2;
    if (hz <= 4) return 3;
    if (hz <= 8) return 4;
    if (hz <= 16) return 5;
    if (hz <= 32) return 6;
    return 7;
}

static bool is_model_compiled(thermal_sensor_model_t model)
{
    if (model == THERMAL_SENSOR_MLX90641) return SUPPORT_MLX90641 != 0;
    if (model == THERMAL_SENSOR_MLX90640) return SUPPORT_MLX90640 != 0;
    return false;
}

static int sensor_set_refresh_rate(uint8_t rate_code)
{
#if SUPPORT_MLX90641
    if (s_model == THERMAL_SENSOR_MLX90641) {
        return MLX90641_SetRefreshRate(MLX90640_I2C_ADDR, rate_code);
    }
#endif
#if SUPPORT_MLX90640
    if (s_model == THERMAL_SENSOR_MLX90640) {
        return MLX90640_SetRefreshRate(MLX90640_I2C_ADDR, rate_code);
    }
#endif
    return -1;
}

static void update_frame_statistics(thermal_frame_t *frame)
{
    int count = (int)frame->width * (int)frame->height;
    if (count <= 0 || count > THERMAL_SENSOR_MAX_PIXELS) {
        frame->min_c = 0.0f;
        frame->max_c = 0.0f;
        frame->avg_c = 0.0f;
        return;
    }

    float min_val = 999.0f;
    float max_val = -999.0f;
    float sum_val = 0.0f;
    int valid_cnt = 0;

    for (int i = 0; i < count; i++) {
        float val = frame->pixels[i];
        if (!isnan(val)) {
            if (val < min_val) min_val = val;
            if (val > max_val) max_val = val;
            sum_val += val;
            valid_cnt++;
        }
    }

    frame->min_c = (valid_cnt > 0 && min_val < 900.0f) ? min_val : 0.0f;
    frame->max_c = (valid_cnt > 0 && max_val > -900.0f) ? max_val : 0.0f;
    frame->avg_c = (valid_cnt > 0) ? (sum_val / (float)valid_cnt) : 0.0f;
}

/**
 * @brief Read one raw subpage from the active sensor, compute pixel temperatures, and apply bad-pixel correction.
 *
 * Caller must hold s_frame_mutex when passing s_cached_frame.pixels directly.
 * Returns subpage number (>= 0) on success or negative driver error code.
 */
static int sensor_acquire_subpage(float *pixels_out, float *out_ta)
{
#if SUPPORT_MLX90641
    if (s_model == THERMAL_SENSOR_MLX90641) {
        int res = MLX90641_GetFrameData(MLX90640_I2C_ADDR, s_frame_data);
        if (res < 0) return res;

        float ta = MLX90641_GetTa(s_frame_data, s_params641);
        MLX90641_CalculateTo(s_frame_data, s_params641, s_emissivity, ta - 8.0f, pixels_out);
        if (s_params641->brokenPixel < MLX90641_NUM_PIXELS) {
            MLX90641_BadPixelsCorrection(s_params641->brokenPixel, pixels_out);
        }
        *out_ta = ta;
        return res;
    }
#endif
#if SUPPORT_MLX90640
    if (s_model == THERMAL_SENSOR_MLX90640) {
        int res = MLX90640_GetFrameData(MLX90640_I2C_ADDR, s_frame_data);
        if (res < 0) return res;

        float ta = MLX90640_GetTa(s_frame_data, s_params640);
        MLX90640_CalculateTo(s_frame_data, s_params640, s_emissivity, ta - 8.0f, pixels_out);
        int mode = MLX90640_GetCurMode(MLX90640_I2C_ADDR);
        MLX90640_BadPixelsCorrection(s_params640->brokenPixels, pixels_out, mode, s_params640);
        MLX90640_BadPixelsCorrection(s_params640->outlierPixels, pixels_out, mode, s_params640);
        *out_ta = ta;
        return res;
    }
#endif
    return -1;
}

static void finalize_frame_locked(const char *mode, float ta)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    s_cached_frame.timestamp_ms = (int64_t)tv.tv_sec * 1000 + (tv.tv_usec / 1000);
    pcf85063_format_iso_time(s_cached_frame.iso_time, sizeof(s_cached_frame.iso_time));

    s_cached_frame.seq = ++s_seq;
    strncpy(s_cached_frame.mode, mode, sizeof(s_cached_frame.mode) - 1);
    s_cached_frame.mode[sizeof(s_cached_frame.mode) - 1] = '\0';
    s_cached_frame.width = mlx_acq_get_width();
    s_cached_frame.height = mlx_acq_get_height();
    s_cached_frame.ambient_c = ta;
    update_frame_statistics(&s_cached_frame);
    s_has_frame = true;
}

static void mlx_acq_task(void *pvParameters)
{
    (void)pvParameters;
    ESP_LOGI(TAG, "%s acquisition task started on Core %d (%ux%u)",
             mlx_acq_get_model_name(), xPortGetCoreID(),
             mlx_acq_get_width(), mlx_acq_get_height());

    while (1) {
        if (!s_running && !s_single_requested) {
            xSemaphoreTake(s_wake_sem, portMAX_DELAY);
        }

        if (s_single_requested) {
            float ta = 0.0f;
            xSemaphoreTake(s_frame_mutex, portMAX_DELAY);
            int res0 = sensor_acquire_subpage(s_cached_frame.pixels, &ta);
            int final_res = res0;

            if (s_model == THERMAL_SENSOR_MLX90640) {
                // MLX90640 alternates checkerboard halves across subpages 0 and 1
                int res1 = -1;
                for (int retries = 0; retries < 5; retries++) {
                    float ta1 = ta;
                    res1 = sensor_acquire_subpage(s_cached_frame.pixels, &ta1);
                    if (res1 >= 0 && (res1 != res0 || res0 < 0)) {
                        ta = ta1;
                        break;
                    }
                }
                if (res1 >= 0) {
                    final_res = res1;
                }
            }

            if (final_res >= 0) {
                finalize_frame_locked("single", ta);
            }
            xSemaphoreGive(s_frame_mutex);

            s_single_result = (final_res >= 0) ? ESP_OK : ESP_FAIL;
            s_single_requested = false;
            xSemaphoreGive(s_single_done_sem);

            if (s_single_result == ESP_OK && s_frame_cb) {
                s_frame_cb(&s_cached_frame);
            }
            continue;
        }

        if (s_running) {
            float ta = 0.0f;
            xSemaphoreTake(s_frame_mutex, portMAX_DELAY);
            int res = sensor_acquire_subpage(s_cached_frame.pixels, &ta);
            if (res >= 0) {
                finalize_frame_locked("continuous", ta);
            }
            xSemaphoreGive(s_frame_mutex);

            if (res >= 0) {
                if (s_frame_cb) {
                    s_frame_cb(&s_cached_frame);
                }
            } else {
                ESP_LOGW(TAG, "%s GetFrameData returned error: %d", mlx_acq_get_model_name(), res);
            }

            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }
}

static int cmd_acq(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: acq <start [hz]|stop>\n");
        return 1;
    }
    if (strcasecmp(argv[1], "start") == 0) {
        uint8_t hz = s_refresh_rate;
        if (argc >= 3) {
            hz = (uint8_t)atoi(argv[2]);
        }
        mlx_acq_start(hz);
        printf("Continuous acquisition started at %d Hz\n", s_refresh_rate);
    } else if (strcasecmp(argv[1], "stop") == 0) {
        mlx_acq_stop();
        printf("Continuous acquisition stopped\n");
    } else {
        printf("Unknown acq command '%s'\n", argv[1]);
        return 1;
    }
    return 0;
}

static int cmd_dump_params(int argc, char **argv)
{
    (void)argc;
    (void)argv;
#if SUPPORT_MLX90641
    if (s_model == THERMAL_SENSOR_MLX90641) {
        if (!s_params641) {
            printf("s_params641 is NULL\n");
            return 1;
        }
        printf("MLX90641 Parameters (16x12):\n");
        printf("  kVdd: %d, vdd25: %d\n", s_params641->kVdd, s_params641->vdd25);
        printf("  KvPTAT: %f, KtPTAT: %f, vPTAT25: %u, alphaPTAT: %f\n",
               s_params641->KvPTAT, s_params641->KtPTAT, (unsigned)s_params641->vPTAT25, s_params641->alphaPTAT);
        printf("  gainEE: %d, tgc: %f, emissivityEE: %f\n",
               s_params641->gainEE, s_params641->tgc, s_params641->emissivityEE);
        printf("  resolutionEE: %u, curResolution: %d, calibrationModeEE: 0x%02X\n",
               (unsigned)s_params641->resolutionEE, MLX90641_GetCurResolution(MLX90640_I2C_ADDR),
               s_params641->calibrationModeEE);
        printf("  cpAlpha: %e, cpOffset: %d, cpKta: %f, cpKv: %f\n",
               s_params641->cpAlpha, s_params641->cpOffset, s_params641->cpKta, s_params641->cpKv);
        printf("  brokenPixel: %u\n", (unsigned)s_params641->brokenPixel);
        return 0;
    }
#endif
#if SUPPORT_MLX90640
    if (s_model == THERMAL_SENSOR_MLX90640) {
        if (!s_params640) {
            printf("s_params640 is NULL\n");
            return 1;
        }
        printf("MLX90640 Parameters (32x24):\n");
        printf("  kVdd: %d, vdd25: %d\n", s_params640->kVdd, s_params640->vdd25);
        printf("  KvPTAT: %f, KtPTAT: %f, vPTAT25: %u, alphaPTAT: %f\n",
               s_params640->KvPTAT, s_params640->KtPTAT, (unsigned)s_params640->vPTAT25, s_params640->alphaPTAT);
        printf("  resolutionEE: %u, tgc: %f\n", (unsigned)s_params640->resolutionEE, s_params640->tgc);
        printf("  calibrationModeEE: 0x%02X, curMode: %d\n",
               s_params640->calibrationModeEE, MLX90640_GetCurMode(MLX90640_I2C_ADDR));
        printf("  cpAlpha[0]: %e, cpAlpha[1]: %e\n", s_params640->cpAlpha[0], s_params640->cpAlpha[1]);
        printf("  cpOffset[0]: %d, cpOffset[1]: %d\n", s_params640->cpOffset[0], s_params640->cpOffset[1]);
        printf("  cpKta: %f, cpKv: %f\n", s_params640->cpKta, s_params640->cpKv);
        printf("  brokenPixels: %u %u %u %u %u\n",
               s_params640->brokenPixels[0], s_params640->brokenPixels[1], s_params640->brokenPixels[2],
               s_params640->brokenPixels[3], s_params640->brokenPixels[4]);
        return 0;
    }
#endif
    return 1;
}

static int cmd_dump_ee(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    uint16_t *buf = malloc(64 * sizeof(uint16_t));
    if (!buf) return 1;
    int err = MLX90640_I2CRead(MLX90640_I2C_ADDR, 0x2400, 64, buf);
    if (err != 0) {
        printf("I2C read EEPROM failed: %d\n", err);
        free(buf);
        return 1;
    }
    printf("EEPROM [0x2400..0x243F]:\n");
    for (int i = 0; i < 64; i++) {
        printf("%04X ", buf[i]);
        if ((i + 1) % 16 == 0) printf("\n");
    }
    free(buf);
    return 0;
}

static int cmd_dump_aux(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    uint16_t base_addr = (s_model == THERMAL_SENSOR_MLX90641) ? 0x0580 : 0x0700;
    uint16_t word_cnt = (s_model == THERMAL_SENSOR_MLX90641) ? 48 : 64;

    uint16_t *buf = malloc(word_cnt * sizeof(uint16_t));
    if (!buf) return 1;
    int err = MLX90640_I2CRead(MLX90640_I2C_ADDR, base_addr, word_cnt, buf);
    if (err != 0) {
        printf("I2C read AUX failed: %d\n", err);
        free(buf);
        return 1;
    }
    uint16_t stat = 0, ctrl = 0;
    MLX90640_I2CRead(MLX90640_I2C_ADDR, 0x8000, 1, &stat);
    MLX90640_I2CRead(MLX90640_I2C_ADDR, 0x800D, 1, &ctrl);
    printf("STAT: 0x%04X, CTRL: 0x%04X\n", stat, ctrl);
    printf("AUX RAM [0x%04X..0x%04X] (%u words):\n", base_addr, base_addr + word_cnt - 1, word_cnt);
    for (int i = 0; i < word_cnt; i++) {
        printf("%04X ", buf[i]);
        if ((i + 1) % 16 == 0) printf("\n");
    }
    free(buf);
    return 0;
}

static int cmd_capture(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    thermal_frame_t *frame = malloc(sizeof(thermal_frame_t));
    if (!frame) {
        printf("Error: Out of memory\n");
        return 1;
    }
    esp_err_t err = mlx_acq_capture_single(frame, 2500);
    if (err == ESP_ERR_INVALID_STATE) {
        printf("Error: Continuous acquisition is running! Stop continuous mode first with 'acq stop'.\n");
        free(frame);
        return 1;
    } else if (err != ESP_OK) {
        printf("Error: Frame capture failed (%s)\n", esp_err_to_name(err));
        free(frame);
        return 1;
    }
    int center_idx = (frame->height / 2) * frame->width + (frame->width / 2);
    printf("Single Capture Result (%s %ux%u):\n", mlx_acq_get_model_name(), frame->width, frame->height);
    printf("  Seq: %lu, Ambient: %.2f C, Min: %.2f C, Max: %.2f C, Avg: %.2f C\n",
           frame->seq, frame->ambient_c, frame->min_c, frame->max_c, frame->avg_c);
    printf("  Center pixel [%d]: %.2f C\n", center_idx, frame->pixels[center_idx]);
    free(frame);
    return 0;
}

esp_err_t mlx_acq_parse_model(const char *model_str, thermal_sensor_model_t *out_model)
{
    ESP_RETURN_ON_FALSE(model_str != NULL && out_model != NULL, ESP_ERR_INVALID_ARG, TAG, "Invalid argument");
    if (strcasecmp(model_str, "mlx90641") == 0) {
        *out_model = THERMAL_SENSOR_MLX90641;
        return ESP_OK;
    }
    if (strcasecmp(model_str, "mlx90640") == 0) {
        *out_model = THERMAL_SENSOR_MLX90640;
        return ESP_OK;
    }
    return ESP_ERR_INVALID_ARG;
}

esp_err_t mlx_acq_init(i2c_master_bus_handle_t bus_handle, uint8_t initial_refresh_rate,
                       float emissivity, bool auto_start)
{
    return mlx_acq_init_with_model(bus_handle, DEFAULT_SENSOR_MODEL, initial_refresh_rate, emissivity, auto_start);
}

esp_err_t mlx_acq_init_with_model(i2c_master_bus_handle_t bus_handle, thermal_sensor_model_t model,
                                  uint8_t initial_refresh_rate, float emissivity, bool auto_start)
{
    ESP_RETURN_ON_FALSE(bus_handle != NULL, ESP_ERR_INVALID_ARG, TAG, "bus_handle is NULL");
    ESP_RETURN_ON_FALSE(is_model_compiled(model), ESP_ERR_NOT_SUPPORTED, TAG,
                        "Selected sensor model is not enabled in Kconfig build");

    s_model = model;
    ESP_LOGI(TAG, "Initializing %s acquisition engine (%ux%u)...",
             mlx_acq_get_model_name(), mlx_acq_get_width(), mlx_acq_get_height());

    // Allocate calibration parameter struct & working buffers in PSRAM (with DRAM fallback)
#if SUPPORT_MLX90641
    if (s_model == THERMAL_SENSOR_MLX90641) {
        s_params641 = heap_caps_malloc(sizeof(paramsMLX90641), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_params641) s_params641 = malloc(sizeof(paramsMLX90641));
        ESP_RETURN_ON_FALSE(s_params641 != NULL, ESP_ERR_NO_MEM, TAG, "Failed to allocate paramsMLX90641");
    }
#endif
#if SUPPORT_MLX90640
    if (s_model == THERMAL_SENSOR_MLX90640) {
        s_params640 = heap_caps_malloc(sizeof(paramsMLX90640), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_params640) s_params640 = malloc(sizeof(paramsMLX90640));
        ESP_RETURN_ON_FALSE(s_params640 != NULL, ESP_ERR_NO_MEM, TAG, "Failed to allocate paramsMLX90640");
    }
#endif

    size_t frame_words = (s_model == THERMAL_SENSOR_MLX90641) ? MLX90641_FRAME_DATA_WORDS : MLX90640_FRAME_DATA_WORDS;
    s_frame_data = heap_caps_malloc(frame_words * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_frame_data) {
        s_frame_data = malloc(frame_words * sizeof(uint16_t));
    }
    ESP_RETURN_ON_FALSE(s_frame_data != NULL, ESP_ERR_NO_MEM, TAG, "Failed to allocate frame_data buffer");

    uint16_t *ee_data = heap_caps_malloc(MLX90640_EEPROM_WORDS * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!ee_data) {
        ee_data = malloc(MLX90640_EEPROM_WORDS * sizeof(uint16_t));
    }
    ESP_RETURN_ON_FALSE(ee_data != NULL, ESP_ERR_NO_MEM, TAG, "Failed to allocate eeprom buffer");

    // Initialize shared I2C sensor device
    ESP_RETURN_ON_ERROR(mlx90640_i2c_init(bus_handle, &s_mlx_dev), TAG, "Failed to init thermal sensor I2C device");

    // Dump EEPROM and extract calibration parameters for selected chip
    ESP_LOGI(TAG, "Dumping %s EEPROM calibration parameters...", mlx_acq_get_model_name());
    int dump_res = -1;
    int extract_res = -1;

#if SUPPORT_MLX90641
    if (s_model == THERMAL_SENSOR_MLX90641) {
        dump_res = MLX90641_DumpEE(MLX90640_I2C_ADDR, ee_data);
        if (dump_res != 0) {
            ESP_LOGE(TAG, "Failed to dump MLX90641 EEPROM (%d)", dump_res);
            free(ee_data);
            return ESP_FAIL;
        }
        extract_res = MLX90641_ExtractParameters(ee_data, s_params641);
    }
#endif
#if SUPPORT_MLX90640
    if (s_model == THERMAL_SENSOR_MLX90640) {
        dump_res = MLX90640_DumpEE(MLX90640_I2C_ADDR, ee_data);
        if (dump_res != 0) {
            ESP_LOGE(TAG, "Failed to dump MLX90640 EEPROM (%d)", dump_res);
            free(ee_data);
            return ESP_FAIL;
        }
        extract_res = MLX90640_ExtractParameters(ee_data, s_params640);
    }
#endif

    free(ee_data);
    ESP_LOGI(TAG, "%s ExtractParameters returned: %d", mlx_acq_get_model_name(), extract_res);
    if (extract_res < 0) {
        ESP_LOGW(TAG, "ExtractParameters returned warning/error %d (continuing with bad pixel handling)", extract_res);
    }

    s_refresh_rate = (initial_refresh_rate >= 1 && initial_refresh_rate <= 64) ? initial_refresh_rate : 4;
    s_emissivity = (emissivity >= 0.1f && emissivity <= 1.0f) ? emissivity : 0.95f;
    sensor_set_refresh_rate(hz_to_rate_code(s_refresh_rate));

#if SUPPORT_MLX90640
    if (s_model == THERMAL_SENSOR_MLX90640) {
        MLX90640_SetChessMode(MLX90640_I2C_ADDR);
    }
#endif

    s_frame_mutex = xSemaphoreCreateMutex();
    s_wake_sem = xSemaphoreCreateBinary();
    s_single_done_sem = xSemaphoreCreateBinary();

    s_running = auto_start;
    if (s_running) {
        status_led_set_state(STATUS_LED_STREAMING);
    } else {
        status_led_set_state(STATUS_LED_IDLE);
    }

    // Register console commands
    const esp_console_cmd_t acq_cmd = {
        .command = "acq",
        .help = "Control continuous thermal acquisition: acq <start [hz]|stop>",
        .hint = NULL,
        .func = &cmd_acq,
    };
    esp_console_cmd_register(&acq_cmd);

    const esp_console_cmd_t cap_cmd = {
        .command = "capture",
        .help = "Capture single thermal frame on-demand (when continuous mode is stopped)",
        .hint = NULL,
        .func = &cmd_capture,
    };
    esp_console_cmd_register(&cap_cmd);

    const esp_console_cmd_t params_cmd = {
        .command = "dump_params",
        .help = "Dump extracted thermal sensor parameters",
        .hint = NULL,
        .func = &cmd_dump_params,
    };
    esp_console_cmd_register(&params_cmd);

    const esp_console_cmd_t ee_cmd = {
        .command = "dump_ee",
        .help = "Dump first 64 words of thermal sensor EEPROM",
        .hint = NULL,
        .func = &cmd_dump_ee,
    };
    esp_console_cmd_register(&ee_cmd);

    const esp_console_cmd_t aux_cmd = {
        .command = "dump_aux",
        .help = "Dump AUX RAM for active thermal sensor",
        .hint = NULL,
        .func = &cmd_dump_aux,
    };
    esp_console_cmd_register(&aux_cmd);

    // Spawn acquisition task pinned to Core 1
    xTaskCreatePinnedToCore(mlx_acq_task, "mlx_acq_task", 8192, NULL, 5, NULL, 1);
    return ESP_OK;
}

thermal_sensor_model_t mlx_acq_get_model(void)
{
    return s_model;
}

const char *mlx_acq_get_model_name(void)
{
    return (s_model == THERMAL_SENSOR_MLX90641) ? "mlx90641" : "mlx90640";
}

uint16_t mlx_acq_get_width(void)
{
    return (s_model == THERMAL_SENSOR_MLX90641) ? MLX90641_WIDTH : MLX90640_WIDTH;
}

uint16_t mlx_acq_get_height(void)
{
    return (s_model == THERMAL_SENSOR_MLX90641) ? MLX90641_HEIGHT : MLX90640_HEIGHT;
}

esp_err_t mlx_acq_start(uint8_t refresh_rate_hz)
{
    if (refresh_rate_hz >= 1 && refresh_rate_hz <= 64) {
        s_refresh_rate = refresh_rate_hz;
        sensor_set_refresh_rate(hz_to_rate_code(s_refresh_rate));
    }
    s_running = true;
    status_led_set_state(STATUS_LED_STREAMING);
    xSemaphoreGive(s_wake_sem);
    return ESP_OK;
}

esp_err_t mlx_acq_stop(void)
{
    s_running = false;
    status_led_set_state(STATUS_LED_IDLE);
    return ESP_OK;
}

bool mlx_acq_is_running(void)
{
    return s_running;
}

uint8_t mlx_acq_get_refresh_rate(void)
{
    return s_refresh_rate;
}

float mlx_acq_get_emissivity(void)
{
    return s_emissivity;
}

esp_err_t mlx_acq_capture_single(thermal_frame_t *out_frame, uint32_t timeout_ms)
{
    ESP_RETURN_ON_FALSE(out_frame != NULL, ESP_ERR_INVALID_ARG, TAG, "out_frame is NULL");

    if (s_running) {
        return ESP_ERR_INVALID_STATE;
    }

    s_single_requested = true;
    xSemaphoreGive(s_wake_sem);

    if (xSemaphoreTake(s_single_done_sem, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        s_single_requested = false;
        return ESP_ERR_TIMEOUT;
    }

    if (s_single_result != ESP_OK) {
        return s_single_result;
    }

    xSemaphoreTake(s_frame_mutex, portMAX_DELAY);
    memcpy(out_frame, &s_cached_frame, sizeof(thermal_frame_t));
    xSemaphoreGive(s_frame_mutex);

    return ESP_OK;
}

esp_err_t mlx_acq_get_latest_frame(thermal_frame_t *out_frame)
{
    ESP_RETURN_ON_FALSE(out_frame != NULL, ESP_ERR_INVALID_ARG, TAG, "out_frame is NULL");
    if (!s_has_frame) {
        return ESP_ERR_NOT_FOUND;
    }

    xSemaphoreTake(s_frame_mutex, portMAX_DELAY);
    memcpy(out_frame, &s_cached_frame, sizeof(thermal_frame_t));
    xSemaphoreGive(s_frame_mutex);

    return ESP_OK;
}

esp_err_t mlx_acq_register_frame_cb(mlx_frame_callback_t cb)
{
    s_frame_cb = cb;
    return ESP_OK;
}
