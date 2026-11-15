#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize USB Serial/JTAG console REPL and register diagnostic commands.
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t debug_console_init(void);

#ifdef __cplusplus
}
#endif
