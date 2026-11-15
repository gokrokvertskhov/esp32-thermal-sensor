#pragma once

#include "mlx90640_api.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Serialize a thermal_frame_t into a compact JSON string.
 *
 * @param[in] frame Source thermal frame
 * @param[out] out_buf Destination character buffer
 * @param[in] max_len Capacity of destination buffer (recommended >= 16384)
 * @return int Number of bytes written, or negative on error/buffer too small
 */
int frame_to_json(const thermal_frame_t *frame, char *out_buf, size_t max_len);

#ifdef __cplusplus
}
#endif
