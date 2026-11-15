#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    STATUS_LED_BOOTING,         // Blue pulsing
    STATUS_LED_STREAMING,       // Green solid
    STATUS_LED_IDLE,            // Cyan solid
    STATUS_LED_WIFI_CONNECTING, // Yellow blinking
    STATUS_LED_FAULT,           // Red blinking
} status_led_state_t;

#define STATUS_LED_GPIO_NUM (38)

/**
 * @brief Initialize WS2812 RGB LED on GPIO 38 and start animation timer.
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t status_led_init(void);

/**
 * @brief Set system status LED state.
 *
 * @param[in] state Target state
 */
void status_led_set_state(status_led_state_t state);

/**
 * @brief Get current system status LED state.
 *
 * @return status_led_state_t
 */
status_led_state_t status_led_get_state(void);

#ifdef __cplusplus
}
#endif
