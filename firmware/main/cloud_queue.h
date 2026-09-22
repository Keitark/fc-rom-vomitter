#pragma once

#include "esp_err.h"

typedef enum {
    CLOUD_QUEUE_WAITING,
    CLOUD_QUEUE_INSTALLED,
    CLOUD_QUEUE_DEFERRED,
} cloud_queue_result_t;

esp_err_t cloud_queue_poll(cloud_queue_result_t *result);
