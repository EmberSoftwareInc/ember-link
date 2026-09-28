#pragma once
#include <stdint.h>
#define ESP_APP_DESC_MAGIC_WORD 0xabcd5432
typedef struct { uint32_t magic_word; char project_name[32]; char version[32]; } esp_app_desc_t;
const esp_app_desc_t *esp_app_get_description(void);
