idf_component_register(SRCS "mlx90640_i2c.c" "MLX90640_API.c" "MLX90641_API.c" "mlx90640_controller.c"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_driver_i2c status_led pcf85063 console)
