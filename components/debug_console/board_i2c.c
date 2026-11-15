#include "board_i2c.h"
#include <stdio.h>
#include "esp_log.h"
#include "esp_check.h"

static const char *TAG = "board_i2c";
static i2c_master_bus_handle_t s_bus_handle = NULL;

esp_err_t board_i2c_init(void)
{
    if (s_bus_handle != NULL) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing shared I2C0 on SDA=GPIO%d, SCL=GPIO%d", BOARD_I2C_SDA_GPIO, BOARD_I2C_SCL_GPIO);

    const i2c_master_bus_config_t bus_cfg = {
        .i2c_port = BOARD_I2C_PORT,
        .sda_io_num = BOARD_I2C_SDA_GPIO,
        .scl_io_num = BOARD_I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_bus_handle), TAG, "Failed to create I2C master bus");
    return ESP_OK;
}

i2c_master_bus_handle_t board_i2c_get_bus_handle(void)
{
    return s_bus_handle;
}

esp_err_t board_i2c_scan(void)
{
    if (!s_bus_handle) {
        printf("Error: I2C bus not initialized\n");
        return ESP_ERR_INVALID_STATE;
    }
    printf("Scanning shared I2C bus (SDA=GPIO%d, SCL=GPIO%d)...\n", BOARD_I2C_SDA_GPIO, BOARD_I2C_SCL_GPIO);
    int found_count = 0;
    for (uint16_t addr = 0x08; addr <= 0x77; ++addr) {
        esp_err_t err = i2c_master_probe(s_bus_handle, addr, 50);
        if (err == ESP_OK) {
            found_count++;
            const char *desc = "unknown";
            if (addr == 0x20) desc = "TCA9554 IO Expander";
            else if (addr == 0x33) desc = "MLX90640 Thermal Camera";
            else if (addr == 0x51) desc = "PCF85063 RTC";
            printf("  -> Found device at 0x%02X (%s)\n", addr, desc);
        }
    }
    if (found_count == 0) {
        printf("  No I2C devices found.\n");
    } else {
        printf("Found %d I2C device(s).\n", found_count);
    }
    return ESP_OK;
}
