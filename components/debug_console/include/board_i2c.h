#pragma once

#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BOARD_I2C_PORT      I2C_NUM_0
#define BOARD_I2C_SDA_GPIO  (42)
#define BOARD_I2C_SCL_GPIO  (41)

/**
 * @brief Initialize the shared I2C0 master bus.
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t board_i2c_init(void);

/**
 * @brief Get the shared I2C master bus handle.
 *
 * @return i2c_master_bus_handle_t Bus handle or NULL if not initialized
 */
i2c_master_bus_handle_t board_i2c_get_bus_handle(void);

/**
 * @brief Probe all valid 7-bit addresses on the shared I2C bus and print results.
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t board_i2c_scan(void);

#ifdef __cplusplus
}
#endif
