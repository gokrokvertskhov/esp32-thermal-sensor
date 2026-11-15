#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize and start the HTTP/WebSocket thermal server on port 80.
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t web_server_start(void);

/**
 * @brief Stop the HTTP/WebSocket server.
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t web_server_stop(void);

#ifdef __cplusplus
}
#endif
