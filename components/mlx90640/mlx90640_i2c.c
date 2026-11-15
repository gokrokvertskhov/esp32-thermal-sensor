#include "mlx90640_api.h"
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_check.h"

static const char *TAG = "mlx90640_i2c";
static i2c_master_dev_handle_t s_active_mlx_dev = NULL;

esp_err_t mlx90640_i2c_init(i2c_master_bus_handle_t bus_handle, i2c_master_dev_handle_t *out_dev)
{
    ESP_RETURN_ON_FALSE(bus_handle != NULL, ESP_ERR_INVALID_ARG, TAG, "bus_handle is NULL");
    ESP_RETURN_ON_FALSE(out_dev != NULL, ESP_ERR_INVALID_ARG, TAG, "out_dev is NULL");

    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = MLX90640_I2C_ADDR,
        .scl_speed_hz = 400000, // 400 kHz Fast Mode
    };

    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus_handle, &dev_cfg, out_dev),
                        TAG, "Failed to attach MLX90640 to I2C bus");
    s_active_mlx_dev = *out_dev;
    ESP_LOGI(TAG, "MLX90640 attached on I2C address 0x%02X @ %lu Hz", MLX90640_I2C_ADDR, dev_cfg.scl_speed_hz);
    return ESP_OK;
}

esp_err_t mlx90640_i2c_read(i2c_master_dev_handle_t dev, uint16_t start_addr, uint16_t n_words, uint16_t *data)
{
    ESP_RETURN_ON_FALSE(dev != NULL, ESP_ERR_INVALID_STATE, TAG, "device handle is NULL");
    ESP_RETURN_ON_FALSE(data != NULL, ESP_ERR_INVALID_ARG, TAG, "data buffer is NULL");

    uint8_t cmd[2] = {
        (uint8_t)(start_addr >> 8),
        (uint8_t)(start_addr & 0xFF)
    };

    size_t rx_len = n_words * 2;
    uint8_t *rx_buf = malloc(rx_len);
    if (!rx_buf) {
        ESP_LOGE(TAG, "Failed to allocate %zu bytes for I2C read", rx_len);
        return ESP_ERR_NO_MEM;
    }

    // Timeout: 1000ms for large reads (e.g. 832 words), 200ms for small reads
    int timeout_ms = (n_words > 100) ? 1000 : 200;
    esp_err_t err = i2c_master_transmit_receive(dev, cmd, 2, rx_buf, rx_len, timeout_ms);
    if (err == ESP_OK) {
        for (uint16_t i = 0; i < n_words; i++) {
            data[i] = ((uint16_t)rx_buf[2 * i] << 8) | rx_buf[2 * i + 1];
        }
    } else {
        ESP_LOGE(TAG, "I2C read failed from 0x%04X (%d words): %s", start_addr, n_words, esp_err_to_name(err));
    }

    free(rx_buf);
    return err;
}

esp_err_t mlx90640_i2c_write(i2c_master_dev_handle_t dev, uint16_t write_addr, uint16_t data)
{
    ESP_RETURN_ON_FALSE(dev != NULL, ESP_ERR_INVALID_STATE, TAG, "device handle is NULL");

    uint8_t tx_buf[4] = {
        (uint8_t)(write_addr >> 8),
        (uint8_t)(write_addr & 0xFF),
        (uint8_t)(data >> 8),
        (uint8_t)(data & 0xFF)
    };

    esp_err_t err = i2c_master_transmit(dev, tx_buf, 4, 200);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C write failed to 0x%04X: %s", write_addr, esp_err_to_name(err));
    }
    return err;
}

// Melexis MLX90640_I2C_Driver implementations
int MLX90640_I2CRead(uint8_t slaveAddr, uint16_t startAddress, uint16_t nMemAddressRead, uint16_t *data)
{
    (void)slaveAddr;
    if (!s_active_mlx_dev) return -1;
    return (mlx90640_i2c_read(s_active_mlx_dev, startAddress, nMemAddressRead, data) == ESP_OK) ? 0 : -1;
}

int MLX90640_I2CWrite(uint8_t slaveAddr, uint16_t writeAddress, uint16_t data)
{
    (void)slaveAddr;
    if (!s_active_mlx_dev) return -1;
    return (mlx90640_i2c_write(s_active_mlx_dev, writeAddress, data) == ESP_OK) ? 0 : -1;
}

void MLX90640_I2CInit(void) {}
int MLX90640_I2CGeneralReset(void) { return 0; }
void MLX90640_I2CFreqSet(int freq) { (void)freq; }

// Melexis MLX90641_I2C_Driver implementations (shared I2C HAL)
int MLX90641_I2CRead(uint8_t slaveAddr, uint16_t startAddress, uint16_t nMemAddressRead, uint16_t *data)
{
    return MLX90640_I2CRead(slaveAddr, startAddress, nMemAddressRead, data);
}

int MLX90641_I2CWrite(uint8_t slaveAddr, uint16_t writeAddress, uint16_t data)
{
    return MLX90640_I2CWrite(slaveAddr, writeAddress, data);
}

void MLX90641_I2CInit(void) {}
int MLX90641_I2CGeneralReset(void) { return 0; }
void MLX90641_I2CFreqSet(int freq) { (void)freq; }
