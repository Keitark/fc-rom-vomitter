#pragma once
#include <stdint.h>
typedef uint32_t TickType_t;
typedef int BaseType_t;
#define pdPASS 1
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
