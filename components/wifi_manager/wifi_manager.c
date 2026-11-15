#include "wifi_manager.h"
#include "status_led.h"
#include "pcf85063.h"

#include <string.h>
#include "esp_log.h"
#include "esp_check.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_timer.h"
#include "esp_console.h"

static const char *TAG = "wifi_manager";

static wifi_manager_status_t s_wifi_status = {0};
static uint32_t s_reconnect_delay_ms = 1000;
static esp_timer_handle_t s_reconnect_timer = NULL;
static esp_netif_t *s_sta_netif = NULL;

static void reconnect_timer_cb(void *arg)
{
    ESP_LOGI(TAG, "Attempting Wi-Fi reconnection (backoff: %lu ms)...", s_reconnect_delay_ms);
    esp_wifi_connect();
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "Wi-Fi STA started, connecting to '%s'...", s_wifi_status.ssid);
            status_led_set_state(STATUS_LED_WIFI_CONNECTING);
            esp_wifi_connect();
            break;
        case WIFI_EVENT_STA_CONNECTED:
            ESP_LOGI(TAG, "Wi-Fi connected to AP, waiting for IP...");
            break;
        case WIFI_EVENT_STA_DISCONNECTED: {
            wifi_event_sta_disconnected_t *disc = (wifi_event_sta_disconnected_t *)event_data;
            ESP_LOGW(TAG, "Wi-Fi disconnected (reason=%d)", disc->reason);
            s_wifi_status.connected = false;
            s_wifi_status.ip_addr[0] = '\0';
            status_led_set_state(STATUS_LED_WIFI_CONNECTING);

            // Schedule reconnect with exponential backoff
            if (s_reconnect_timer) {
                esp_timer_stop(s_reconnect_timer);
                esp_timer_start_once(s_reconnect_timer, s_reconnect_delay_ms * 1000);
                if (s_reconnect_delay_ms < 30000) {
                    s_reconnect_delay_ms *= 2;
                    if (s_reconnect_delay_ms > 30000) {
                        s_reconnect_delay_ms = 30000;
                    }
                }
            }
            break;
        }
        default:
            break;
        }
    } else if (event_base == IP_EVENT) {
        if (event_id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
            esp_ip4addr_ntoa(&event->ip_info.ip, s_wifi_status.ip_addr, sizeof(s_wifi_status.ip_addr));
            esp_ip4addr_ntoa(&event->ip_info.netmask, s_wifi_status.netmask, sizeof(s_wifi_status.netmask));
            esp_ip4addr_ntoa(&event->ip_info.gw, s_wifi_status.gateway, sizeof(s_wifi_status.gateway));
            s_wifi_status.connected = true;
            s_reconnect_delay_ms = 1000; // Reset backoff

            wifi_ap_record_t ap_info;
            if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
                s_wifi_status.rssi_dbm = ap_info.rssi;
            }

            ESP_LOGI(TAG, "================================================");
            ESP_LOGI(TAG, " Wi-Fi Connected! IP: %s", s_wifi_status.ip_addr);
            ESP_LOGI(TAG, " Netmask: %s, Gateway: %s, RSSI: %d dBm",
                     s_wifi_status.netmask, s_wifi_status.gateway, s_wifi_status.rssi_dbm);
            ESP_LOGI(TAG, "================================================");

            // Transition LED to idle (will be set to streaming when acq starts)
            status_led_set_state(STATUS_LED_IDLE);

            // Start SNTP time synchronization using configured settings
            const app_config_t *cfg = app_config_get();
            if (cfg) {
                pcf85063_start_sntp_sync(cfg->time.ntp_server, cfg->time.timezone);
            }
        }
    }
}

static int cmd_status(int argc, char **argv)
{
    wifi_manager_status_t st;
    wifi_manager_get_status(&st);

    char iso_time[32];
    pcf85063_format_iso_time(iso_time, sizeof(iso_time));

    printf("Device Status:\n");
    printf("  Wi-Fi Connected : %s\n", st.connected ? "YES" : "NO");
    printf("  SSID            : %s\n", st.ssid);
    printf("  IP Address      : %s\n", st.connected ? st.ip_addr : "N/A");
    printf("  RSSI            : %d dBm\n", st.rssi_dbm);
    printf("  RTC / System ISO: %s\n", iso_time);
    return 0;
}

esp_err_t wifi_manager_init(const app_wifi_config_t *wifi_cfg)
{
    ESP_RETURN_ON_FALSE(wifi_cfg != NULL, ESP_ERR_INVALID_ARG, TAG, "wifi_cfg is NULL");
    ESP_RETURN_ON_FALSE(strlen(wifi_cfg->ssid) > 0, ESP_ERR_INVALID_ARG, TAG, "SSID is empty");

    strncpy(s_wifi_status.ssid, wifi_cfg->ssid, sizeof(s_wifi_status.ssid) - 1);

    // Initialize NVS (needed by Wi-Fi driver)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(ret, TAG, "Failed to init NVS flash");

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "Failed to init netif");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "Failed to create event loop");

    s_sta_netif = esp_netif_create_default_wifi_sta();
    if (strlen(wifi_cfg->hostname) > 0) {
        esp_netif_set_hostname(s_sta_netif, wifi_cfg->hostname);
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "Failed to init Wi-Fi");

    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                           &wifi_event_handler, NULL, NULL),
                        TAG, "Failed to register wifi event handler");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                           &wifi_event_handler, NULL, NULL),
                        TAG, "Failed to register ip event handler");

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, wifi_cfg->ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, wifi_cfg->password, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = (strlen(wifi_cfg->password) > 0) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "Failed to set STA mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_config), TAG, "Failed to set Wi-Fi config");

    // Create reconnect timer
    const esp_timer_create_args_t timer_args = {
        .callback = &reconnect_timer_cb,
        .arg = NULL,
        .name = "wifi_reconnect",
        .dispatch_method = ESP_TIMER_TASK,
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&timer_args, &s_reconnect_timer), TAG, "Failed to create reconnect timer");

    // Register status command in console
    const esp_console_cmd_t status_cmd = {
        .command = "status",
        .help = "Print device status, Wi-Fi IP, and RTC timestamp",
        .hint = NULL,
        .func = &cmd_status,
    };
    esp_console_cmd_register(&status_cmd);

    ESP_LOGI(TAG, "Starting Wi-Fi STA...");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "Failed to start Wi-Fi");
    return ESP_OK;
}

esp_err_t wifi_manager_get_status(wifi_manager_status_t *out_status)
{
    ESP_RETURN_ON_FALSE(out_status != NULL, ESP_ERR_INVALID_ARG, TAG, "out_status is NULL");
    if (s_wifi_status.connected) {
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            s_wifi_status.rssi_dbm = ap_info.rssi;
        }
    }
    memcpy(out_status, &s_wifi_status, sizeof(wifi_manager_status_t));
    return ESP_OK;
}

bool wifi_manager_is_connected(void)
{
    return s_wifi_status.connected;
}
