#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "app_config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool connected;
    char ip_addr[16];
    char netmask[16];
    char gateway[16];
    int8_t rssi_dbm;
    char ssid[APP_CONFIG_MAX_SSID_LEN + 1];
} wifi_manager_status_t;

/**
 * @brief Initialize NVS, Netif, Wi-Fi Station and initiate connection.
 *
 * @param[in] wifi_cfg Pointer to Wi-Fi configuration (SSID, password, hostname)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t wifi_manager_init(const app_wifi_config_t *wifi_cfg);

/**
 * @brief Get current Wi-Fi connection status, IP address, and RSSI.
 *
 * @param[out] out_status Destination status struct
 * @return esp_err_t ESP_OK on success
 */
esp_err_t wifi_manager_get_status(wifi_manager_status_t *out_status);

/**
 * @brief Check if Wi-Fi is currently connected and has an IP.
 *
 * @return true if connected
 */
bool wifi_manager_is_connected(void);

#ifdef __cplusplus
}
#endif
