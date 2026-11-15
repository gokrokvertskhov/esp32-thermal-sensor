#include "frame_json.h"
#include <stdio.h>
#include <inttypes.h>

int frame_to_json(const thermal_frame_t *frame, char *out_buf, size_t max_len)
{
    if (!frame || !out_buf || max_len < 2048) {
        return -1;
    }

    int written = snprintf(out_buf, max_len,
        "{\"seq\":%" PRIu32 ","
        "\"mode\":\"%s\","
        "\"timestamp_ms\":%" PRId64 ","
        "\"iso_time\":\"%s\","
        "\"width\":%u,"
        "\"height\":%u,"
        "\"ambient_c\":%.2f,"
        "\"min_c\":%.2f,"
        "\"max_c\":%.2f,"
        "\"avg_c\":%.2f,"
        "\"pixels\":[",
        frame->seq,
        frame->mode,
        frame->timestamp_ms,
        frame->iso_time,
        (unsigned int)frame->width,
        (unsigned int)frame->height,
        (double)frame->ambient_c,
        (double)frame->min_c,
        (double)frame->max_c,
        (double)frame->avg_c);

    if (written < 0 || (size_t)written >= max_len) {
        return -1;
    }

    int num_pixels = (int)frame->width * (int)frame->height;
    if (num_pixels <= 0 || num_pixels > THERMAL_SENSOR_MAX_PIXELS) {
        return -1;
    }

    for (int i = 0; i < num_pixels; i++) {
        int n = snprintf(out_buf + written, max_len - written,
                         (i == num_pixels - 1) ? "%.2f" : "%.2f,",
                         (double)frame->pixels[i]);
        if (n < 0 || (size_t)(written + n) >= max_len) {
            return -1;
        }
        written += n;
    }

    int end_n = snprintf(out_buf + written, max_len - written, "]}");
    if (end_n < 0 || (size_t)(written + end_n) >= max_len) {
        return -1;
    }
    written += end_n;

    return written;
}
