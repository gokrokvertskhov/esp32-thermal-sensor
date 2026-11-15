#include "app_config.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "esp_log.h"
#include "esp_check.h"
#include "esp_spiffs.h"
#include "cJSON.h"

static const char *TAG = "app_config";
static app_config_t s_app_config;
static bool s_is_loaded = false;

static void set_defaults(app_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    strncpy(cfg->wifi.hostname, APP_CONFIG_DEFAULT_HOSTNAME, sizeof(cfg->wifi.hostname) - 1);
    strncpy(cfg->sensor.model, APP_CONFIG_DEFAULT_SENSOR_MODEL, sizeof(cfg->sensor.model) - 1);
    cfg->sensor.refresh_rate_hz = APP_CONFIG_DEFAULT_REFRESH_RATE_HZ;
    cfg->sensor.emissivity = APP_CONFIG_DEFAULT_EMISSIVITY;
    cfg->sensor.auto_start_continuous = APP_CONFIG_DEFAULT_AUTO_START;
    strncpy(cfg->time.ntp_server, APP_CONFIG_DEFAULT_NTP_SERVER, sizeof(cfg->time.ntp_server) - 1);
    strncpy(cfg->time.timezone, APP_CONFIG_DEFAULT_TIMEZONE, sizeof(cfg->time.timezone) - 1);
}

esp_err_t app_config_load_from_str(const char *json_str, app_config_t *out_config)
{
    ESP_RETURN_ON_FALSE(json_str != NULL, ESP_ERR_INVALID_ARG, TAG, "json_str is NULL");
    ESP_RETURN_ON_FALSE(out_config != NULL, ESP_ERR_INVALID_ARG, TAG, "out_config is NULL");

    set_defaults(out_config);

    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse JSON string");
        return ESP_ERR_INVALID_ARG;
    }

    // Wi-Fi object (required)
    cJSON *wifi = cJSON_GetObjectItemCaseSensitive(root, "wifi");
    if (!cJSON_IsObject(wifi)) {
        ESP_LOGE(TAG, "Missing 'wifi' object in config");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *ssid = cJSON_GetObjectItemCaseSensitive(wifi, "ssid");
    if (!cJSON_IsString(ssid) || (ssid->valuestring == NULL) || (strlen(ssid->valuestring) == 0)) {
        ESP_LOGE(TAG, "Missing or empty 'wifi.ssid'");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }
    if (strlen(ssid->valuestring) > APP_CONFIG_MAX_SSID_LEN) {
        ESP_LOGE(TAG, "'wifi.ssid' exceeds max length of %d", APP_CONFIG_MAX_SSID_LEN);
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }
    strncpy(out_config->wifi.ssid, ssid->valuestring, sizeof(out_config->wifi.ssid) - 1);

    cJSON *password = cJSON_GetObjectItemCaseSensitive(wifi, "password");
    if (cJSON_IsString(password) && (password->valuestring != NULL)) {
        strncpy(out_config->wifi.password, password->valuestring, sizeof(out_config->wifi.password) - 1);
    }

    cJSON *hostname = cJSON_GetObjectItemCaseSensitive(wifi, "hostname");
    if (cJSON_IsString(hostname) && (hostname->valuestring != NULL) && (strlen(hostname->valuestring) > 0)) {
        strncpy(out_config->wifi.hostname, hostname->valuestring, sizeof(out_config->wifi.hostname) - 1);
    }

    // Sensor object (optional)
    cJSON *sensor = cJSON_GetObjectItemCaseSensitive(root, "sensor");
    if (cJSON_IsObject(sensor)) {
        cJSON *model = cJSON_GetObjectItemCaseSensitive(sensor, "model");
        if (cJSON_IsString(model) && (model->valuestring != NULL) && (strlen(model->valuestring) > 0)) {
            if (strcasecmp(model->valuestring, "mlx90641") == 0) {
                strncpy(out_config->sensor.model, "mlx90641", sizeof(out_config->sensor.model) - 1);
            } else if (strcasecmp(model->valuestring, "mlx90640") == 0) {
                strncpy(out_config->sensor.model, "mlx90640", sizeof(out_config->sensor.model) - 1);
            } else {
                ESP_LOGW(TAG, "Unsupported sensor.model '%s', defaulting to '%s'",
                         model->valuestring, APP_CONFIG_DEFAULT_SENSOR_MODEL);
            }
        }

        cJSON *rate = cJSON_GetObjectItemCaseSensitive(sensor, "refresh_rate_hz");
        if (cJSON_IsNumber(rate)) {
            int r = rate->valueint;
            if (r >= 1 && r <= 64) {
                out_config->sensor.refresh_rate_hz = (uint8_t)r;
            } else {
                ESP_LOGW(TAG, "refresh_rate_hz out of range (1..64), defaulting to %d", APP_CONFIG_DEFAULT_REFRESH_RATE_HZ);
            }
        }

        cJSON *emiss = cJSON_GetObjectItemCaseSensitive(sensor, "emissivity");
        if (cJSON_IsNumber(emiss)) {
            double e = emiss->valuedouble;
            if (e >= 0.1 && e <= 1.0) {
                out_config->sensor.emissivity = (float)e;
            } else {
                ESP_LOGW(TAG, "emissivity out of range (0.1..1.0), defaulting to %.2f", (double)APP_CONFIG_DEFAULT_EMISSIVITY);
            }
        }

        cJSON *auto_start = cJSON_GetObjectItemCaseSensitive(sensor, "auto_start_continuous");
        if (cJSON_IsBool(auto_start)) {
            out_config->sensor.auto_start_continuous = cJSON_IsTrue(auto_start);
        }
    }

    // Time object (optional)
    cJSON *time_obj = cJSON_GetObjectItemCaseSensitive(root, "time");
    if (cJSON_IsObject(time_obj)) {
        cJSON *ntp = cJSON_GetObjectItemCaseSensitive(time_obj, "ntp_server");
        if (cJSON_IsString(ntp) && (ntp->valuestring != NULL) && (strlen(ntp->valuestring) > 0)) {
            strncpy(out_config->time.ntp_server, ntp->valuestring, sizeof(out_config->time.ntp_server) - 1);
        }

        cJSON *tz = cJSON_GetObjectItemCaseSensitive(time_obj, "timezone");
        if (cJSON_IsString(tz) && (tz->valuestring != NULL) && (strlen(tz->valuestring) > 0)) {
            strncpy(out_config->time.timezone, tz->valuestring, sizeof(out_config->time.timezone) - 1);
        }
    }

    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t app_config_init(app_config_t *out_config)
{
    ESP_LOGI(TAG, "Mounting SPIFFS 'storage' partition...");

    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = "storage",
        .max_files = 5,
        .format_if_mount_failed = false,
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition 'storage'");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return ret;
    }

    size_t total = 0, used = 0;
    ret = esp_spiffs_info("storage", &total, &used);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "SPIFFS total: %zu KB, used: %zu KB", total / 1024, used / 1024);
    }

    const char *config_path = "/spiffs/config.json";
    FILE *f = fopen(config_path, "r");
    if (!f) {
        ESP_LOGW(TAG, "Cannot open %s, attempting /spiffs/config.example.json", config_path);
        config_path = "/spiffs/config.example.json";
        f = fopen(config_path, "r");
        if (!f) {
            ESP_LOGE(TAG, "No config file found in /spiffs");
            return ESP_ERR_NOT_FOUND;
        }
    }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fsize <= 0 || fsize > 4096) {
        ESP_LOGE(TAG, "Invalid config file size: %ld bytes", fsize);
        fclose(f);
        return ESP_ERR_INVALID_SIZE;
    }

    char *buf = malloc(fsize + 1);
    if (!buf) {
        ESP_LOGE(TAG, "Failed to allocate memory for config file");
        fclose(f);
        return ESP_ERR_NO_MEM;
    }

    size_t read_bytes = fread(buf, 1, fsize, f);
    fclose(f);
    buf[read_bytes] = '\0';

    ret = app_config_load_from_str(buf, &s_app_config);
    free(buf);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to parse config from %s", config_path);
        return ret;
    }

    s_is_loaded = true;
    ESP_LOGI(TAG, "Config loaded successfully. SSID='%s', Hostname='%s', RefreshRate=%d Hz",
             s_app_config.wifi.ssid, s_app_config.wifi.hostname, s_app_config.sensor.refresh_rate_hz);

    if (out_config) {
        memcpy(out_config, &s_app_config, sizeof(app_config_t));
    }
    return ESP_OK;
}

const app_config_t *app_config_get(void)
{
    return s_is_loaded ? &s_app_config : NULL;
}
