#pragma once

#include "esp_http_server.h"

/* Receive and install one raw iNES request. */
esp_err_t web_upload_handler(httpd_req_t *request);
