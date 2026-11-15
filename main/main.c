#include <stdio.h>
#include "esp_log.h"
#include "app_config.h"
#include "status_led.h"
#include "board_i2c.h"
#include "debug_console.h"
#include "pcf85063.h"
#include "wifi_manager.h"
#include "mlx90640_controller.h"
#include "web_server.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "Starting Thermal Server...");

    // 1. Initialize status LED (defaults to Booting: pulsing blue)
    if (status_led_init() == ESP_OK) {
        status_led_set_state(STATUS_LED_BOOTING);
    }

    // 2. Initialize shared I2C bus
    if (board_i2c_init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize shared I2C bus");
        status_led_set_state(STATUS_LED_FAULT);
    }

    // 3. Initialize PCF85063 RTC on shared I2C bus
    i2c_master_bus_handle_t bus_handle = board_i2c_get_bus_handle();
    if (bus_handle != NULL) {
        pcf85063_init(bus_handle);
    }

    // 4. Initialize SPIFFS and load configuration
    app_config_t config;
    esp_err_t err = app_config_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize app config: %s", esp_err_to_name(err));
        status_led_set_state(STATUS_LED_FAULT);
    } else {
        ESP_LOGI(TAG, "Config loaded: SSID='%s', Hostname='%s'", config.wifi.ssid, config.wifi.hostname);
    }

    // 5. Initialize USB Serial/JTAG debug console REPL
    if (debug_console_init() != ESP_OK) {
        ESP_LOGW(TAG, "Failed to start debug console");
    }

    // 6. Initialize thermal sensor engine (MLX90641 or MLX90640 per config)
    if (bus_handle != NULL) {
        thermal_sensor_model_t model = THERMAL_SENSOR_MLX90641;
        if (err == ESP_OK) {
            if (mlx_acq_parse_model(config.sensor.model, &model) != ESP_OK) {
                ESP_LOGW(TAG, "Unknown sensor model '%s', falling back to default", config.sensor.model);
            }
        }
        uint8_t rate = (err == ESP_OK) ? config.sensor.refresh_rate_hz : 4;
        float emiss = (err == ESP_OK) ? config.sensor.emissivity : 0.95f;
        bool auto_start = (err == ESP_OK) ? config.sensor.auto_start_continuous : true;

        esp_err_t mlx_err = mlx_acq_init_with_model(bus_handle, model, rate, emiss, auto_start);
        if (mlx_err != ESP_OK) {
            ESP_LOGW(TAG, "Thermal sensor initialization failed or sensor not connected: %s",
                     esp_err_to_name(mlx_err));
        }
    }

    // 7. Connect to Wi-Fi (initializes esp_netif / lwIP tcpip stack before starting web server)
    if (err == ESP_OK) {
        esp_err_t wifi_err = wifi_manager_init(&config.wifi);
        if (wifi_err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize Wi-Fi manager: %s", esp_err_to_name(wifi_err));
            status_led_set_state(STATUS_LED_FAULT);
        }
    }

    // 8. Start HTTP & WebSocket Web Server
    esp_err_t ws_err = web_server_start();
    if (ws_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start web server: %s", esp_err_to_name(ws_err));
    }
}
