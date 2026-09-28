#pragma once
#include <stdbool.h>
#include "esp_err.h"
// One mutating operation across LAN, cloud, OTA, and setup. Nonblocking.
esp_err_t operation_init(void);
bool operation_begin(void);
void operation_end(void);
