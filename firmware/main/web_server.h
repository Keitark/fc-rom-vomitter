#pragma once

#include "esp_err.h"

esp_err_t web_server_start(void);
esp_err_t web_server_stop_for_sdr(void);
esp_err_t web_server_resume_after_sdr_failure(void);
