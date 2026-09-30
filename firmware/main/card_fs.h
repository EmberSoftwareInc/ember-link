#pragma once
#include "esp_err.h"
#include "sdmmc_cmd.h"
#include "card_layout.h"
// Only use before registering MSC, or in CDC-only maintenance mode.
esp_err_t card_fs_inspect(sdmmc_card_t *card, card_info_t *info);
esp_err_t card_fs_format(sdmmc_card_t *card, card_info_t *info);

// Set EMBER LINK on an existing FAT32 card, preserving files and layout.
esp_err_t card_fs_rename(sdmmc_card_t *card, card_info_t *info);
