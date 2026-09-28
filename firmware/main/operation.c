#include "operation.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
static SemaphoreHandle_t s_gate;
esp_err_t operation_init(void)
{
    s_gate = xSemaphoreCreateMutex();
    return s_gate ? ESP_OK : ESP_ERR_NO_MEM;
}
bool operation_begin(void)
{
    return s_gate && xSemaphoreTake(s_gate, 0) == pdTRUE;
}
void operation_end(void)
{
    xSemaphoreGive(s_gate);
}
