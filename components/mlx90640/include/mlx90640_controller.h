#pragma once

#include "mlx90640_api.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*mlx_frame_callback_t)(const thermal_frame_t *frame);

/**
 * @brief Initialize thermal sensor on the I2C bus using the build-time default model.
 *
 * @param[in] bus_handle Shared I2C master bus handle
 * @param[in] initial_refresh_rate Initial rate in Hz (1, 2, 4, 8, 16, 32, 64)
 * @param[in] emissivity Surface emissivity (e.g. 0.95)
 * @param[in] auto_start Start continuous acquisition immediately
 * @return esp_err_t ESP_OK on success
 */
esp_err_t mlx_acq_init(i2c_master_bus_handle_t bus_handle, uint8_t initial_refresh_rate,
                       float emissivity, bool auto_start);

/**
 * @brief Initialize thermal sensor with explicit chip model selection (MLX90641 or MLX90640).
 *
 * @param[in] bus_handle Shared I2C master bus handle
 * @param[in] model Sensor model enum (THERMAL_SENSOR_MLX90641 or THERMAL_SENSOR_MLX90640)
 * @param[in] initial_refresh_rate Initial rate in Hz (1..64)
 * @param[in] emissivity Surface emissivity (e.g. 0.95)
 * @param[in] auto_start Start continuous acquisition immediately
 * @return esp_err_t ESP_OK on success
 */
esp_err_t mlx_acq_init_with_model(i2c_master_bus_handle_t bus_handle, thermal_sensor_model_t model,
                                  uint8_t initial_refresh_rate, float emissivity, bool auto_start);

/**
 * @brief Parse sensor model string ("mlx90641" or "mlx90640") into enum.
 *
 * @param[in] model_str Input model string (case-insensitive)
 * @param[out] out_model Parsed model enum
 * @return esp_err_t ESP_OK if valid, ESP_ERR_INVALID_ARG otherwise
 */
esp_err_t mlx_acq_parse_model(const char *model_str, thermal_sensor_model_t *out_model);

/**
 * @brief Get active thermal sensor model.
 */
thermal_sensor_model_t mlx_acq_get_model(void);

/**
 * @brief Get active thermal sensor model name ("mlx90641" or "mlx90640").
 */
const char *mlx_acq_get_model_name(void);

/**
 * @brief Get active sensor frame width in pixels (16 or 32).
 */
uint16_t mlx_acq_get_width(void);

/**
 * @brief Get active sensor frame height in pixels (12 or 24).
 */
uint16_t mlx_acq_get_height(void);

/**
 * @brief Start continuous thermal frame acquisition.
 *
 * @param[in] refresh_rate_hz Target rate in Hz (1..8)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t mlx_acq_start(uint8_t refresh_rate_hz);

/**
 * @brief Stop continuous thermal frame acquisition.
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t mlx_acq_stop(void);

/**
 * @brief Check if continuous acquisition is currently active.
 *
 * @return true if running
 */
bool mlx_acq_is_running(void);

/**
 * @brief Get configured refresh rate in Hz.
 *
 * @return uint8_t
 */
uint8_t mlx_acq_get_refresh_rate(void);

/**
 * @brief Get configured emissivity.
 *
 * @return float
 */
float mlx_acq_get_emissivity(void);

/**
 * @brief Trigger an on-demand single-frame capture when continuous mode is stopped.
 *
 * @param[out] out_frame Pointer to receive the captured frame
 * @param[in] timeout_ms Maximum time to wait for capture (e.g. 1500ms)
 * @return esp_err_t ESP_OK on success, ESP_ERR_INVALID_STATE if continuous mode is running,
 *                   ESP_ERR_TIMEOUT on timeout
 */
esp_err_t mlx_acq_capture_single(thermal_frame_t *out_frame, uint32_t timeout_ms);

/**
 * @brief Retrieve a thread-safe copy of the latest cached frame.
 *
 * @param[out] out_frame Pointer to receive the latest frame
 * @return esp_err_t ESP_OK on success, ESP_ERR_NOT_FOUND if no frame captured yet
 */
esp_err_t mlx_acq_get_latest_frame(thermal_frame_t *out_frame);

/**
 * @brief Register a callback invoked whenever a new frame is acquired.
 *
 * @param[in] cb Callback function
 * @return esp_err_t ESP_OK on success
 */
esp_err_t mlx_acq_register_frame_cb(mlx_frame_callback_t cb);

#ifdef __cplusplus
}
#endif
