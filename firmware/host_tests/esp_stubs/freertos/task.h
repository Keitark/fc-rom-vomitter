#pragma once
#include "FreeRTOS.h"
typedef void *TaskHandle_t;
typedef void (*TaskFunction_t)(void *);
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t entry, const char *name,
                                  uint32_t stack, void *context, unsigned priority,
                                  TaskHandle_t *handle, BaseType_t core);
void vTaskDelay(TickType_t delay);
void vTaskDelayUntil(TickType_t *wake, TickType_t period);
void vTaskDelete(TaskHandle_t task);
TickType_t xTaskGetTickCount(void);
