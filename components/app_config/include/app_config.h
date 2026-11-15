#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_CONFIG_MAX_SSID_LEN 32
#define APP_CONFIG_MAX_PASSWORD_LEN 64
#define APP_CONFIG_MAX_HOSTNAME_LEN 32
#define APP_CONFIG_MAX_MODEL_LEN 16
#define APP_CONFIG_MAX_NTP_SERVER_LEN 64
#define APP_CONFIG_MAX_TIMEZONE_LEN 64

#ifdef CONFIG_THERMAL_SENSOR_DEFAULT_MLX90640
#define APP_CONFIG_DEFAULT_SENSOR_MODEL "mlx90640"
#else
#define APP_CONFIG_DEFAULT_SENSOR_MODEL "mlx90641"
#endif
#define APP_CONFIG_DEFAULT_REFRESH_RATE_HZ 4
#define APP_CONFIG_DEFAULT_EMISSIVITY 0.95f
#define APP_CONFIG_DEFAULT_AUTO_START true
#define APP_CONFIG_DEFAULT_HOSTNAME "thermal-server"
#define APP_CONFIG_DEFAULT_NTP_SERVER "pool.ntp.org"
#define APP_CONFIG_DEFAULT_TIMEZONE "PST8PDT,M3.2.0,M11.1.0"

typedef struct {
    char ssid[APP_CONFIG_MAX_SSID_LEN + 1];
    char password[APP_CONFIG_MAX_PASSWORD_LEN + 1];
    char hostname[APP_CONFIG_MAX_HOSTNAME_LEN + 1];
} app_wifi_config_t;

typedef struct {
    char model[APP_CONFIG_MAX_MODEL_LEN + 1];
    uint8_t refresh_rate_hz;
    float emissivity;
    bool auto_start_continuous;
} app_sensor_config_t;

typedef struct {
    char ntp_server[APP_CONFIG_MAX_NTP_SERVER_LEN + 1];
    char timezone[APP_CONFIG_MAX_TIMEZONE_LEN + 1];
} app_time_config_t;

typedef struct {
    app_wifi_config_t wifi;
    app_sensor_config_t sensor;
    app_time_config_t time;
} app_config_t;

/**
 * @brief Parse JSON string into app_config_t structure with validation.
 *
 * @param[in] json_str JSON null-terminated string
 * @param[out] out_config Pointer to destination app_config_t
 * @return esp_err_t ESP_OK on success, ESP_ERR_INVALID_ARG if required fields missing or out of bounds.
 */
esp_err_t app_config_load_from_str(const char *json_str, app_config_t *out_config);

/**
 * @brief Initialize SPIFFS, mount /spiffs, and load /spiffs/config.json.
 *
 * @param[out] out_config Optional destination for loaded config (can be NULL)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t app_config_init(app_config_t *out_config);

/**
 * @brief Get the globally loaded configuration instance.
 *
 * @return const app_config_t* Pointer to config, or NULL if not loaded
 */
const app_config_t *app_config_get(void);

#ifdef __cplusplus
}
#endif
