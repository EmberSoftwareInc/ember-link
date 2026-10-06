// SD card + USB mass-storage arbitration.
//
// The microSD card has exactly one owner at a time:
//   - USB: the embroidery machine sees the card as a plugged-in memory stick.
//   - APP: the firmware has the FAT volume mounted at storage_base_path()
//          and can read/write files; the machine sees "no medium".
//
// Normal state is USB. WiFi uploads briefly claim the card
// (storage_acquire), write, then hand it back (storage_release) — which
// also drops and re-raises the USB data lines so the machine re-reads the
// filesystem, exactly as if the stick had been physically re-plugged.
//
// Because listing files requires owning the card, directory contents and
// volume stats are cached at boot and refreshed on every release; the
// read-only API endpoints serve the cache and never disturb the machine.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "card_layout.h"

#define STORAGE_MAX_FILES 128
#define STORAGE_MAX_NAME 128

typedef struct {
    char name[STORAGE_MAX_NAME];
    uint64_t size;
} storage_file_t;

// setup_mode selects MSC+CDC for a computer; otherwise exposes MSC only.
// Missing/unreadable cards leave storage unavailable but do not block setup.
// In maintenance mode only CDC is exposed, with no network/storage clients.
//
// Once provisioned, remove the legacy "START HERE.html" helper created by
// older firmware. New firmware never creates this file.
esp_err_t storage_init(bool provisioned, bool setup_mode, void (*waiting_for_card_cb)(void));

// Claim the card for firmware file access. Blocks other claimants.
esp_err_t storage_acquire(void);

// Refresh caches, return the card to the USB host, and re-plug the USB
// device so the machine rescans the filesystem.
esp_err_t storage_release(void);

const char *storage_base_path(void);

// Cached views — safe to call any time, never touch the card.
size_t storage_cached_files(storage_file_t *out, size_t max);
void storage_cached_stats(uint64_t *total_bytes, uint64_t *free_bytes);

bool storage_ready(void);
void storage_card_info(card_info_t *out);
// Caller must hold the operation gate; only CDC-only maintenance may format.
esp_err_t storage_format_card(void);

esp_err_t storage_rename_card(void);
