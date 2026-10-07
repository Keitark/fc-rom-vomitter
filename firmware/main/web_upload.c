#include "web_upload.h"

#include <stdbool.h>
#include <stdint.h>

#include "controller.h"
#include "esp_timer.h"
#include "ines.h"

#define UPLOAD_RECEIVE_BUDGET_US INT64_C(30000000)

typedef struct {
    httpd_req_t *request;
    int64_t deadline_us;
    bool read_failed;
} upload_reader_t;

static esp_err_t upload_error(httpd_req_t *request, const char *status,
                              const char *message)
{
    (void)httpd_resp_set_hdr(request, "Connection", "close");
    (void)httpd_resp_send_custom_err(request, status, message);
    /* ESP_OK would make HTTPD synchronously drain the unread request body. */
    return ESP_FAIL;
}

static int upload_read_exact(void *context, uint8_t *output, size_t length)
{
    upload_reader_t *reader = context;
    size_t received = 0;
    unsigned consecutive_timeouts = 0;
    while (received < length) {
        if (esp_timer_get_time() >= reader->deadline_us) {
            reader->read_failed = true;
            return -1;
        }
        const size_t remaining = length - received;
        const size_t chunk = remaining < 1024u ? remaining : 1024u;
        const int result = httpd_req_recv(reader->request,
                                          (char *)output + received, chunk);
        /* A final receive must not turn an expired upload into a commit. */
        if (esp_timer_get_time() >= reader->deadline_us) {
            reader->read_failed = true;
            return -1;
        }
        if (result == HTTPD_SOCK_ERR_TIMEOUT) {
            if (++consecutive_timeouts < 3u) {
                continue;
            }
            reader->read_failed = true;
            return -1;
        }
        if (result <= 0) {
            reader->read_failed = true;
            return -1;
        }
        consecutive_timeouts = 0;
        received += (size_t)result;
    }
    return 0;
}

esp_err_t web_upload_handler(httpd_req_t *request)
{
    if (request->content_len == 0 || request->content_len > NESCART_MAX_INES_SIZE) {
        return upload_error(request, "413 Payload Too Large",
                            "Invalid or oversized iNES upload");
    }
    upload_reader_t reader = {
        .request = request,
        .deadline_us = esp_timer_get_time() + UPLOAD_RECEIVE_BUDGET_US,
    };
    char error[160];
    const esp_err_t err = controller_install_ines_stream(
        upload_read_exact, &reader, (size_t)request->content_len,
        error, sizeof(error));
    if (err != ESP_OK) {
        return upload_error(request, reader.read_failed
                                       ? "408 Request Timeout"
                                       : err == ESP_ERR_INVALID_ARG
                                             ? "400 Bad Request"
                                             : "500 Internal Server Error",
                            error);
    }
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_sendstr(request,
                              "{\"ok\":true,\"next\":\"Wait for solid LED, then press console RESET\"}");
}
