#pragma once

#include <stddef.h>
#include "esp_err.h"

typedef struct {
    size_t content_len;
} httpd_req_t;

#define HTTPD_SOCK_ERR_TIMEOUT -3

int httpd_req_recv(httpd_req_t *request, char *buffer, size_t length);
esp_err_t httpd_resp_set_status(httpd_req_t *request, const char *status);
esp_err_t httpd_resp_set_type(httpd_req_t *request, const char *type);
esp_err_t httpd_resp_set_hdr(httpd_req_t *request, const char *field, const char *value);
esp_err_t httpd_resp_sendstr(httpd_req_t *request, const char *body);
esp_err_t httpd_resp_send_custom_err(httpd_req_t *request, const char *status,
                                    const char *message);
