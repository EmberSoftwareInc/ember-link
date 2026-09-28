#pragma once
#include "FreeRTOS.h"
int xTaskCreate(void (*fn)(void *), const char *, unsigned, void *, unsigned, void *);
void vTaskDelay(TickType_t);
typedef void *TaskHandle_t;
uint32_t ulTaskNotifyTake(int, TickType_t);
void xTaskNotifyGive(TaskHandle_t);
void vTaskDelete(void *);
