#include "pcf85063.h"

#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include "esp_log.h"
#include "esp_check.h"
#include "esp_netif_sntp.h"

static const char *TAG = "pcf85063";

static i2c_master_dev_handle_t s_dev_handle = NULL;
static bool s_rtc_valid = false;

static inline uint8_t bcd2dec(uint8_t val)
{
    return (val >> 4) * 10 + (val & 0x0F);
}

static inline uint8_t dec2bcd(uint8_t val)
{
    return ((val / 10) << 4) | (val % 10);
}

esp_err_t pcf85063_init(i2c_master_bus_handle_t bus_handle)
{
    ESP_RETURN_ON_FALSE(bus_handle != NULL, ESP_ERR_INVALID_ARG, TAG, "bus_handle is NULL");

    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = PCF85063_I2C_ADDR,
        .scl_speed_hz = 400000,
    };

    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus_handle, &dev_cfg, &s_dev_handle),
                        TAG, "Failed to add PCF85063 to I2C bus");

    // Read RTC time to see if oscillator is running and restore system time
    struct tm tm_now;
    esp_err_t err = pcf85063_get_time(&tm_now);
    if (err == ESP_OK) {
        time_t t = mktime(&tm_now);
        if (t != (time_t)-1) {
            struct timeval tv = { .tv_sec = t, .tv_usec = 0 };
            settimeofday(&tv, NULL);
            s_rtc_valid = true;
            char buf[64];
            strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_now);
            ESP_LOGI(TAG, "Restored system clock from PCF85063 RTC: %s UTC", buf);
        }
    } else {
        ESP_LOGW(TAG, "PCF85063 RTC oscillator stopped or uninitialized (%s)", esp_err_to_name(err));
    }

    return ESP_OK;
}

esp_err_t pcf85063_get_time(struct tm *timeinfo)
{
    ESP_RETURN_ON_FALSE(s_dev_handle != NULL, ESP_ERR_INVALID_STATE, TAG, "RTC device not initialized");
    ESP_RETURN_ON_FALSE(timeinfo != NULL, ESP_ERR_INVALID_ARG, TAG, "timeinfo is NULL");

    uint8_t reg = 0x04; // Seconds register
    uint8_t data[7] = {0};

    ESP_RETURN_ON_ERROR(i2c_master_transmit_receive(s_dev_handle, &reg, 1, data, sizeof(data), 100),
                        TAG, "Failed to read RTC registers");

    // If bit 7 of seconds register (OS / oscillator stop) is set, clock integrity not guaranteed
    if (data[0] & 0x80) {
        return ESP_ERR_INVALID_STATE;
    }

    memset(timeinfo, 0, sizeof(*timeinfo));
    timeinfo->tm_sec = bcd2dec(data[0] & 0x7F);
    timeinfo->tm_min = bcd2dec(data[1] & 0x7F);
    timeinfo->tm_hour = bcd2dec(data[2] & 0x3F);
    timeinfo->tm_mday = bcd2dec(data[3] & 0x3F);
    timeinfo->tm_wday = bcd2dec(data[4] & 0x07);
    timeinfo->tm_mon = bcd2dec(data[5] & 0x1F) - 1; // 0..11
    timeinfo->tm_year = bcd2dec(data[6]) + 100;      // 2000+ -> years since 1900

    return ESP_OK;
}

esp_err_t pcf85063_set_time(const struct tm *timeinfo)
{
    ESP_RETURN_ON_FALSE(s_dev_handle != NULL, ESP_ERR_INVALID_STATE, TAG, "RTC device not initialized");
    ESP_RETURN_ON_FALSE(timeinfo != NULL, ESP_ERR_INVALID_ARG, TAG, "timeinfo is NULL");

    uint8_t data[8];
    data[0] = 0x04; // Seconds register start
    data[1] = dec2bcd(timeinfo->tm_sec) & 0x7F; // clear OS bit
    data[2] = dec2bcd(timeinfo->tm_min) & 0x7F;
    data[3] = dec2bcd(timeinfo->tm_hour) & 0x3F;
    data[4] = dec2bcd(timeinfo->tm_mday) & 0x3F;
    data[5] = dec2bcd(timeinfo->tm_wday) & 0x07;
    data[6] = dec2bcd(timeinfo->tm_mon + 1) & 0x1F;
    int yr = timeinfo->tm_year - 100;
    if (yr < 0) yr = 0;
    data[7] = dec2bcd(yr % 100);

    ESP_RETURN_ON_ERROR(i2c_master_transmit(s_dev_handle, data, sizeof(data), 100),
                        TAG, "Failed to write RTC registers");

    s_rtc_valid = true;
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", timeinfo);
    ESP_LOGI(TAG, "PCF85063 hardware RTC time updated: %s UTC", buf);
    return ESP_OK;
}

static void sntp_sync_notification_cb(struct timeval *tv)
{
    time_t now = tv->tv_sec;
    struct tm timeinfo;
    gmtime_r(&now, &timeinfo);

    char strftime_buf[64];
    strftime(strftime_buf, sizeof(strftime_buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
    ESP_LOGI(TAG, "SNTP time synced: %s UTC", strftime_buf);

    if (s_dev_handle) {
        pcf85063_set_time(&timeinfo);
    }
}

esp_err_t pcf85063_start_sntp_sync(const char *ntp_server, const char *timezone)
{
    if (timezone && strlen(timezone) > 0) {
        setenv("TZ", timezone, 1);
        tzset();
    }

    const char *server = (ntp_server && strlen(ntp_server) > 0) ? ntp_server : "pool.ntp.org";
    ESP_LOGI(TAG, "Starting SNTP client with server: %s", server);

    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(server);
    config.sync_cb = sntp_sync_notification_cb;

    return esp_netif_sntp_init(&config);
}

esp_err_t pcf85063_format_iso_time(char *buf, size_t max_len)
{
    ESP_RETURN_ON_FALSE(buf != NULL && max_len >= 21, ESP_ERR_INVALID_ARG, TAG, "invalid buffer");

    time_t now;
    time(&now);
    struct tm timeinfo;
    gmtime_r(&now, &timeinfo);

    if (timeinfo.tm_year < (2020 - 1900)) {
        // Time not set or unverified
        snprintf(buf, max_len, "1970-01-01T00:00:00Z");
    } else {
        strftime(buf, max_len, "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
    }
    return ESP_OK;
}
