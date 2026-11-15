idf_component_register(SRCS "web_server.c" "frame_json.c"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_http_server mlx90640 wifi_manager pcf85063 espressif__cjson)
