#pragma once
#include <stdbool.h>
#include "esp_err.h"

/* Apply before AP start/restart. Does not modify the station/RF interface. */
esp_err_t ap_compat_apply(bool allow_ht);
