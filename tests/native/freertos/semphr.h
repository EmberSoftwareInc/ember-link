#pragma once
#include "FreeRTOS.h"
typedef int *SemaphoreHandle_t;
SemaphoreHandle_t xSemaphoreCreateMutex(void);
int xSemaphoreTake(SemaphoreHandle_t, TickType_t);
void xSemaphoreGive(SemaphoreHandle_t);
