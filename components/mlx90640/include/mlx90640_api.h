#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2c_master.h"
#include "MLX90640_API.h"
#include "MLX90640_I2C_Driver.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MLX90640_I2C_ADDR           (0x33)
#define MLX90640_WIDTH              (32)
#define MLX90640_HEIGHT             (24)
#define MLX90640_NUM_PIXELS         (768)   // 32 x 24
#define MLX90640_FRAME_DATA_WORDS   (834)
#define MLX90640_EEPROM_WORDS       (832)

#define MLX90641_WIDTH              (16)
#define MLX90641_HEIGHT             (12)
#define MLX90641_NUM_PIXELS         (192)   // 16 x 12
#define MLX90641_FRAME_DATA_WORDS   (242)
#define MLX90641_EEPROM_WORDS       (832)

#define THERMAL_SENSOR_MAX_PIXELS   (MLX90640_NUM_PIXELS)
#define THERMAL_SENSOR_MAX_FRAME_WORDS (MLX90640_FRAME_DATA_WORDS)

typedef enum {
    THERMAL_SENSOR_MLX90641 = 0,
    THERMAL_SENSOR_MLX90640 = 1,
} thermal_sensor_model_t;

typedef struct {
    uint32_t seq;
    char mode[16];          // "continuous" or "single"
    int64_t timestamp_ms;
    char iso_time[32];
    uint16_t width;         // 16 (MLX90641) or 32 (MLX90640)
    uint16_t height;        // 12 (MLX90641) or 24 (MLX90640)
    float ambient_c;
    float min_c;
    float max_c;
    float avg_c;
    float pixels[THERMAL_SENSOR_MAX_PIXELS];
} thermal_frame_t;

// Low-level I2C helpers for ESP-IDF
esp_err_t mlx90640_i2c_init(i2c_master_bus_handle_t bus_handle, i2c_master_dev_handle_t *out_dev);
esp_err_t mlx90640_i2c_read(i2c_master_dev_handle_t dev, uint16_t start_addr, uint16_t n_words, uint16_t *data);
esp_err_t mlx90640_i2c_write(i2c_master_dev_handle_t dev, uint16_t write_addr, uint16_t data);

#ifdef __cplusplus
}
#endif
