#pragma once

#include <time.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PCF85063_I2C_ADDR   (0x51)

/**
 * @brief Initialize PCF85063 device on the shared I2C bus and restore system time.
 *
 * @param[in] bus_handle Shared I2C master bus handle
 * @return esp_err_t ESP_OK on success
 */
esp_err_t pcf85063_init(i2c_master_bus_handle_t bus_handle);

/**
 * @brief Read current date and time from the PCF85063 hardware RTC.
 *
 * @param[out] timeinfo Pointer to struct tm destination
 * @return esp_err_t ESP_OK on success
 */
esp_err_t pcf85063_get_time(struct tm *timeinfo);

/**
 * @brief Write date and time to the PCF85063 hardware RTC.
 *
 * @param[in] timeinfo Pointer to source struct tm
 * @return esp_err_t ESP_OK on success
 */
esp_err_t pcf85063_set_time(const struct tm *timeinfo);

/**
 * @brief Configure timezone and start SNTP time sync service.
 *        When NTP time is acquired, PCF85063 hardware RTC is automatically updated.
 *
 * @param[in] ntp_server NTP server hostname or IP (e.g. "pool.ntp.org")
 * @param[in] timezone POSIX timezone string (e.g. "PST8PDT,M3.2.0,M11.1.0")
 * @return esp_err_t ESP_OK on success
 */
esp_err_t pcf85063_start_sntp_sync(const char *ntp_server, const char *timezone);

/**
 * @brief Format current system/RTC time as ISO 8601 string (e.g. "2026-11-14T20:45:00Z").
 *
 * @param[out] buf Buffer to receive formatted string
 * @param[in] max_len Size of buffer
 * @return esp_err_t ESP_OK on success
 */
esp_err_t pcf85063_format_iso_time(char *buf, size_t max_len);

#ifdef __cplusplus
}
#endif
