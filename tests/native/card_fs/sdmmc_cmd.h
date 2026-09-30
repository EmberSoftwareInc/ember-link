#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
typedef struct { struct { uint32_t capacity; unsigned sector_size; } csd; } sdmmc_card_t;
esp_err_t sdmmc_get_status(sdmmc_card_t *card);
esp_err_t sdmmc_write_sectors(sdmmc_card_t *card, const void *data, size_t sector, size_t count);
