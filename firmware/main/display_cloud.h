#pragma once
#include "cJSON.h"
#include "esp_err.h"
#include <stdint.h>
#include <time.h>
// Add current persisted settings and capabilities to USB info/polls.
void display_settings_add_json(cJSON *body);
void display_cloud_add_poll(cJSON *body);
// Callers hold the operation gate for these mutations.
esp_err_t display_cloud_run(const cJSON *command, uint64_t generation, time_t now);
esp_err_t display_cloud_ack(const cJSON *ack);
