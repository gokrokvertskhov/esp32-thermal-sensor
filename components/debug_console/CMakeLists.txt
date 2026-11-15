idf_component_register(SRCS "board_i2c.c" "debug_console.c"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_driver_i2c console)
