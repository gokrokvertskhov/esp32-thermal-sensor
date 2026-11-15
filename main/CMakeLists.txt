idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES app_config status_led debug_console pcf85063 wifi_manager mlx90640 web_server)

spiffs_create_partition_image(storage ../spiffs_data FLASH_IN_PROJECT)





