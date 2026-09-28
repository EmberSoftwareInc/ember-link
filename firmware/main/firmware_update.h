#pragma once
#include "cJSON.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#define LINK_BOARD_ID "lilygo-t-dongle-s3"
#define LINK_LAYOUT_ID "link-v1"
esp_err_t firmware_update_init(void);
bool firmware_update_pending(void);
// Public compatibility metadata only; never includes cloud receipts or credentials.
void firmware_update_add_capabilities(cJSON *body);
void firmware_update_add_poll(cJSON *body);
void firmware_update_ack(const cJSON *ack);
void firmware_update_run(const cJSON *job, uint64_t generation, const char *host);
