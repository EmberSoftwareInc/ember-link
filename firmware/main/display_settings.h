#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
typedef struct { bool enabled; uint16_t rotation; bool led_enabled; } display_settings_t;
display_settings_t display_settings_defaults(void);
void display_settings_load(display_settings_t *out);
esp_err_t display_settings_save(display_settings_t settings);
