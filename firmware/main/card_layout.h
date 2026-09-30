#pragma once
#include <stdbool.h>
#include <stdint.h>
// Conservative DIY formatting range: 64 MiB through 32 GiB, 512-byte sectors.
// Machine support still needs qualification; this is not a compatibility claim.
bool card_layout_supported(uint64_t sectors, unsigned sector_size);
bool card_layout_mbr(uint8_t out[512], uint64_t sectors, unsigned sector_size);
unsigned card_layout_cluster_size(uint64_t sectors);

#define CARD_VOLUME_LABEL "EMBER LINK"

typedef struct {
    bool present, readable, fat32, can_format;
    uint64_t capacity_bytes;
    char label[12]; // FAT volume label, sanitized ASCII for JSON display.
} card_info_t;
