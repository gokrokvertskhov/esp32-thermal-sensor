#include "status_led.h"

#include <string.h>
#include <math.h>
#include "esp_log.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "led_strip.h"

static const char *TAG = "status_led";
static led_strip_handle_t s_led_strip = NULL;
static status_led_state_t s_current_state = STATUS_LED_BOOTING;
static esp_timer_handle_t s_anim_timer = NULL;
static uint32_t s_anim_tick = 0;

static void anim_timer_callback(void *arg)
{
    if (!s_led_strip) {
        return;
    }

    s_anim_tick++;
    uint32_t r = 0, g = 0, b = 0;

    switch (s_current_state) {
    case STATUS_LED_BOOTING: {
        // Blue pulsing: period ~2 seconds (40 ticks @ 50ms)
        uint32_t phase = s_anim_tick % 40;
        float brightness = 0.5f * (1.0f - cosf(phase * 2.0f * (float)M_PI / 40.0f));
        b = (uint32_t)(brightness * 40.0f);
        break;
    }
    case STATUS_LED_STREAMING:
        // Green solid
        g = 30;
        break;
    case STATUS_LED_IDLE:
        // Cyan solid
        g = 20;
        b = 20;
        break;
    case STATUS_LED_WIFI_CONNECTING:
        // Yellow blinking (500ms on, 500ms off = 10 ticks on, 10 ticks off)
        if ((s_anim_tick % 20) < 10) {
            r = 30;
            g = 25;
        }
        break;
    case STATUS_LED_FAULT:
        // Red fast blinking (200ms on, 200ms off = 4 ticks on, 4 ticks off)
        if ((s_anim_tick % 8) < 4) {
            r = 45;
        }
        break;
    default:
        break;
    }

    led_strip_set_pixel(s_led_strip, 0, r, g, b);
    led_strip_refresh(s_led_strip);
}

esp_err_t status_led_init(void)
{
    ESP_LOGI(TAG, "Initializing WS2812 RGB LED on GPIO %d...", STATUS_LED_GPIO_NUM);

    led_strip_config_t strip_config = {
        .strip_gpio_num = STATUS_LED_GPIO_NUM,
        .max_leds = 1,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags = {
            .invert_out = false,
        }
    };

    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000, // 10MHz
        .flags = {
            .with_dma = false,
        }
    };

    ESP_RETURN_ON_ERROR(led_strip_new_rmt_device(&strip_config, &rmt_config, &s_led_strip),
                        TAG, "Failed to create RMT led strip device");

    led_strip_clear(s_led_strip);

    const esp_timer_create_args_t timer_args = {
        .callback = &anim_timer_callback,
        .arg = NULL,
        .name = "status_led_anim",
        .dispatch_method = ESP_TIMER_TASK,
    };

    ESP_RETURN_ON_ERROR(esp_timer_create(&timer_args, &s_anim_timer), TAG, "Failed to create anim timer");
    ESP_RETURN_ON_ERROR(esp_timer_start_periodic(s_anim_timer, 50000), TAG, "Failed to start anim timer"); // 50ms

    ESP_LOGI(TAG, "Status LED initialized");
    return ESP_OK;
}

void status_led_set_state(status_led_state_t state)
{
    s_current_state = state;
}

status_led_state_t status_led_get_state(void)
{
    return s_current_state;
}
