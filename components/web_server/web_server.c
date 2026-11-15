#include "web_server.h"
#include "frame_json.h"
#include "mlx90640_controller.h"
#include "wifi_manager.h"
#include "pcf85063.h"

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_check.h"
#include "esp_http_server.h"
#include "esp_heap_caps.h"
#include "cJSON.h"

static const char *TAG = "web_server";

#define MAX_WS_CLIENTS 8
#define JSON_BUF_SIZE 16384

static httpd_handle_t s_server = NULL;
static int s_ws_client_fds[MAX_WS_CLIENTS];
static int s_ws_client_count = 0;
static SemaphoreHandle_t s_ws_mutex = NULL;

static char *s_json_buf = NULL;
static SemaphoreHandle_t s_json_mutex = NULL;

static void ws_add_client(int fd)
{
    xSemaphoreTake(s_ws_mutex, portMAX_DELAY);
    for (int i = 0; i < s_ws_client_count; i++) {
        if (s_ws_client_fds[i] == fd) {
            xSemaphoreGive(s_ws_mutex);
            return;
        }
    }
    if (s_ws_client_count < MAX_WS_CLIENTS) {
        s_ws_client_fds[s_ws_client_count++] = fd;
        ESP_LOGI(TAG, "WebSocket client joined (fd=%d, total=%d)", fd, s_ws_client_count);
    } else {
        ESP_LOGW(TAG, "Max WebSocket clients reached, rejecting fd=%d", fd);
    }
    xSemaphoreGive(s_ws_mutex);
}

static void ws_remove_client(int fd)
{
    xSemaphoreTake(s_ws_mutex, portMAX_DELAY);
    for (int i = 0; i < s_ws_client_count; i++) {
        if (s_ws_client_fds[i] == fd) {
            for (int j = i; j < s_ws_client_count - 1; j++) {
                s_ws_client_fds[j] = s_ws_client_fds[j + 1];
            }
            s_ws_client_count--;
            ESP_LOGI(TAG, "WebSocket client left (fd=%d, remaining=%d)", fd, s_ws_client_count);
            break;
        }
    }
    xSemaphoreGive(s_ws_mutex);
}

static void broadcast_frame_to_ws(const thermal_frame_t *frame)
{
    if (!s_server || s_ws_client_count == 0) {
        return;
    }

    xSemaphoreTake(s_json_mutex, portMAX_DELAY);
    int len = frame_to_json(frame, s_json_buf, JSON_BUF_SIZE);
    if (len <= 0) {
        xSemaphoreGive(s_json_mutex);
        return;
    }

    httpd_ws_frame_t ws_pkt = {
        .final = true,
        .fragmented = false,
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)s_json_buf,
        .len = (size_t)len,
    };

    xSemaphoreTake(s_ws_mutex, portMAX_DELAY);
    int active_fds[MAX_WS_CLIENTS];
    int count = s_ws_client_count;
    memcpy(active_fds, s_ws_client_fds, count * sizeof(int));
    xSemaphoreGive(s_ws_mutex);

    for (int i = 0; i < count; i++) {
        esp_err_t err = httpd_ws_send_frame_async(s_server, active_fds[i], &ws_pkt);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Async WS send failed for fd=%d (%s), removing", active_fds[i], esp_err_to_name(err));
            ws_remove_client(active_fds[i]);
        }
    }
    xSemaphoreGive(s_json_mutex);
}

// GET / - Serve embedded index.html
static esp_err_t get_root_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");

    FILE *f = fopen("/spiffs/index.html", "r");
    if (!f) {
        const char *fallback = "<html><body><h1>Thermal Server</h1><p>index.html not found on /spiffs</p></body></html>";
        return httpd_resp_send(req, fallback, HTTPD_RESP_USE_STRLEN);
    }

    char chunk[1024];
    size_t read_bytes = 0;
    while ((read_bytes = fread(chunk, 1, sizeof(chunk), f)) > 0) {
        if (httpd_resp_send_chunk(req, chunk, read_bytes) != ESP_OK) {
            fclose(f);
            return ESP_FAIL;
        }
    }
    fclose(f);
    return httpd_resp_send_chunk(req, NULL, 0); // End of response
}

// GET /api/v1/status
static esp_err_t get_status_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");

    wifi_manager_status_t wifi_st;
    wifi_manager_get_status(&wifi_st);

    char iso_time[32];
    pcf85063_format_iso_time(iso_time, sizeof(iso_time));

    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "wifi_connected", wifi_st.connected);
    cJSON_AddStringToObject(root, "ip", wifi_st.connected ? wifi_st.ip_addr : "N/A");
    cJSON_AddNumberToObject(root, "rssi_dbm", wifi_st.rssi_dbm);
    cJSON_AddStringToObject(root, "iso_time", iso_time);
    cJSON_AddStringToObject(root, "sensor_model", mlx_acq_get_model_name());
    cJSON_AddNumberToObject(root, "width", mlx_acq_get_width());
    cJSON_AddNumberToObject(root, "height", mlx_acq_get_height());
    cJSON_AddBoolToObject(root, "acquisition_running", mlx_acq_is_running());
    cJSON_AddNumberToObject(root, "refresh_rate_hz", mlx_acq_get_refresh_rate());
    cJSON_AddNumberToObject(root, "emissivity", (double)mlx_acq_get_emissivity());
    cJSON_AddNumberToObject(root, "ws_clients", s_ws_client_count);

    const char *resp = cJSON_PrintUnformatted(root);
    esp_err_t err = httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    cJSON_free((void *)resp);
    cJSON_Delete(root);
    return err;
}

// POST /api/v1/acquisition/start
static esp_err_t post_acq_start_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");

    uint8_t hz = mlx_acq_get_refresh_rate();
    if (req->content_len > 0 && req->content_len < 256) {
        char buf[256];
        int ret = httpd_req_recv(req, buf, req->content_len);
        if (ret > 0) {
            buf[ret] = '\0';
            cJSON *root = cJSON_Parse(buf);
            if (root) {
                cJSON *rate = cJSON_GetObjectItemCaseSensitive(root, "refresh_rate_hz");
                if (cJSON_IsNumber(rate) && rate->valueint >= 1 && rate->valueint <= 64) {
                    hz = (uint8_t)rate->valueint;
                }
                cJSON_Delete(root);
            }
        }
    }

    mlx_acq_start(hz);

    char resp[128];
    snprintf(resp, sizeof(resp),
             "{\"acquisition_running\":true,\"refresh_rate_hz\":%d,\"emissivity\":%.2f}",
             mlx_acq_get_refresh_rate(), (double)mlx_acq_get_emissivity());
    return httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
}

// POST /api/v1/acquisition/stop
static esp_err_t post_acq_stop_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    mlx_acq_stop();

    char resp[128];
    snprintf(resp, sizeof(resp),
             "{\"acquisition_running\":false,\"refresh_rate_hz\":%d,\"emissivity\":%.2f}",
             mlx_acq_get_refresh_rate(), (double)mlx_acq_get_emissivity());
    return httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
}

// GET /api/v1/frame
static esp_err_t get_frame_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");

    thermal_frame_t *frame = malloc(sizeof(thermal_frame_t));
    if (!frame) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        return httpd_resp_send(req, "{\"error\":\"out_of_memory\"}", HTTPD_RESP_USE_STRLEN);
    }

    esp_err_t err = mlx_acq_get_latest_frame(frame);
    if (err == ESP_ERR_NOT_FOUND) {
        free(frame);
        httpd_resp_set_status(req, "404 Not Found");
        return httpd_resp_send(req, "{\"error\":\"no_frame_available\"}", HTTPD_RESP_USE_STRLEN);
    } else if (err != ESP_OK) {
        free(frame);
        httpd_resp_set_status(req, "500 Internal Server Error");
        return httpd_resp_send(req, "{\"error\":\"frame_fetch_error\"}", HTTPD_RESP_USE_STRLEN);
    }

    xSemaphoreTake(s_json_mutex, portMAX_DELAY);
    int len = frame_to_json(frame, s_json_buf, JSON_BUF_SIZE);
    free(frame);
    if (len <= 0) {
        xSemaphoreGive(s_json_mutex);
        httpd_resp_set_status(req, "500 Internal Server Error");
        return httpd_resp_send(req, "{\"error\":\"serialization_failed\"}", HTTPD_RESP_USE_STRLEN);
    }

    err = httpd_resp_send(req, s_json_buf, len);
    xSemaphoreGive(s_json_mutex);
    return err;
}

// POST and GET /api/v1/frame/capture
static esp_err_t capture_frame_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");

    if (mlx_acq_is_running()) {
        httpd_resp_set_status(req, "409 Conflict");
        return httpd_resp_send(req,
            "{\"error\":\"continuous_acquisition_active\",\"message\":\"Stop continuous acquisition before requesting a single-shot capture, or use GET /api/v1/frame\"}",
            HTTPD_RESP_USE_STRLEN);
    }

    thermal_frame_t *frame = malloc(sizeof(thermal_frame_t));
    if (!frame) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        return httpd_resp_send(req, "{\"error\":\"out_of_memory\"}", HTTPD_RESP_USE_STRLEN);
    }

    esp_err_t err = mlx_acq_capture_single(frame, 2500);
    if (err == ESP_ERR_INVALID_STATE) {
        free(frame);
        httpd_resp_set_status(req, "409 Conflict");
        return httpd_resp_send(req,
            "{\"error\":\"continuous_acquisition_active\"}",
            HTTPD_RESP_USE_STRLEN);
    } else if (err != ESP_OK) {
        free(frame);
        httpd_resp_set_status(req, "500 Internal Server Error");
        return httpd_resp_send(req, "{\"error\":\"capture_failed\"}", HTTPD_RESP_USE_STRLEN);
    }

    xSemaphoreTake(s_json_mutex, portMAX_DELAY);
    int len = frame_to_json(frame, s_json_buf, JSON_BUF_SIZE);
    free(frame);
    if (len <= 0) {
        xSemaphoreGive(s_json_mutex);
        httpd_resp_set_status(req, "500 Internal Server Error");
        return httpd_resp_send(req, "{\"error\":\"serialization_failed\"}", HTTPD_RESP_USE_STRLEN);
    }

    err = httpd_resp_send(req, s_json_buf, len);
    xSemaphoreGive(s_json_mutex);
    return err;
}

// WS post-handshake callback (called by ESP-IDF v6.1+ after WS upgrade succeeds)
#ifdef CONFIG_HTTPD_WS_POST_HANDSHAKE_CB_SUPPORT
static esp_err_t ws_post_handshake_cb(httpd_req_t *req)
{
    int fd = httpd_req_to_sockfd(req);
    ws_add_client(fd);
    return ESP_OK;
}
#endif

// WS /api/v1/stream
static esp_err_t ws_stream_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        int fd = httpd_req_to_sockfd(req);
        ws_add_client(fd);
        return ESP_OK;
    }

    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        return ret;
    }

    if (ws_pkt.type == HTTPD_WS_TYPE_CLOSE) {
        int fd = httpd_req_to_sockfd(req);
        ws_remove_client(fd);
    }
    return ESP_OK;
}

esp_err_t web_server_start(void)
{
    if (s_server != NULL) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Starting HTTP/WebSocket server on port 80...");

    // Allocate JSON buffer in PSRAM
    s_json_buf = heap_caps_malloc(JSON_BUF_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_json_buf) {
        s_json_buf = malloc(JSON_BUF_SIZE);
    }
    ESP_RETURN_ON_FALSE(s_json_buf != NULL, ESP_ERR_NO_MEM, TAG, "Failed to allocate json buffer");

    s_json_mutex = xSemaphoreCreateMutex();
    s_ws_mutex = xSemaphoreCreateMutex();

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_open_sockets = 10;
    config.stack_size = 8192;
    config.lru_purge_enable = true;

    ESP_RETURN_ON_ERROR(httpd_start(&s_server, &config), TAG, "Failed to start httpd");

    const httpd_uri_t uri_root = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = get_root_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(s_server, &uri_root);

    const httpd_uri_t uri_status = {
        .uri = "/api/v1/status",
        .method = HTTP_GET,
        .handler = get_status_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(s_server, &uri_status);

    const httpd_uri_t uri_acq_start = {
        .uri = "/api/v1/acquisition/start",
        .method = HTTP_POST,
        .handler = post_acq_start_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(s_server, &uri_acq_start);

    const httpd_uri_t uri_acq_stop = {
        .uri = "/api/v1/acquisition/stop",
        .method = HTTP_POST,
        .handler = post_acq_stop_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(s_server, &uri_acq_stop);

    const httpd_uri_t uri_frame = {
        .uri = "/api/v1/frame",
        .method = HTTP_GET,
        .handler = get_frame_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(s_server, &uri_frame);

    const httpd_uri_t uri_capture_post = {
        .uri = "/api/v1/frame/capture",
        .method = HTTP_POST,
        .handler = capture_frame_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(s_server, &uri_capture_post);

    const httpd_uri_t uri_capture_get = {
        .uri = "/api/v1/frame/capture",
        .method = HTTP_GET,
        .handler = capture_frame_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(s_server, &uri_capture_get);

    const httpd_uri_t uri_ws = {
        .uri = "/api/v1/stream",
        .method = HTTP_GET,
        .handler = ws_stream_handler,
        .user_ctx = NULL,
        .is_websocket = true,
        .handle_ws_control_frames = true,
#ifdef CONFIG_HTTPD_WS_POST_HANDSHAKE_CB_SUPPORT
        .ws_post_handshake_cb = ws_post_handshake_cb,
#endif
    };
    httpd_register_uri_handler(s_server, &uri_ws);

    // Register frame callback to broadcast to connected WebSocket clients
    mlx_acq_register_frame_cb(broadcast_frame_to_ws);

    ESP_LOGI(TAG, "HTTP & WebSocket server started successfully");
    return ESP_OK;
}

esp_err_t web_server_stop(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
    return ESP_OK;
}
