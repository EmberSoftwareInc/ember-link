// Signed over-the-air updates. The HTTP layer streams a firmware image in;
// this module writes it to the inactive OTA slot and only makes it bootable
// if the appended RSA-3072 signature verifies against the public key baked
// into the running app (CONFIG_SECURE_SIGNED_ON_UPDATE_NO_SECURE_BOOT).
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

typedef struct {
    char version[32];      // running firmware version (from the app descriptor)
    char slot[17];         // partition label we booted from, e.g. "ota_0"
    bool pending_verify;   // true right after an update, until ota_confirm()
    size_t max_image_size; // size of the inactive slot = largest accepted image
} ota_status_t;

void ota_get_status(ota_status_t *out);

// Streaming update session (one at a time; begin fails with
// ESP_ERR_INVALID_STATE while another is running).
esp_err_t ota_begin(size_t image_size);
// Returns ESP_ERR_INVALID_ARG if the image header says this is not
// Ember Link firmware for this chip.
esp_err_t ota_write(const void *data, size_t len);
// Verifies the signature and full image checksum; only on success does the
// bootloader target switch to the new slot. ESP_ERR_OTA_VALIDATE_FAILED
// means the image is corrupt or not signed with our key.
esp_err_t ota_finish(void);
void ota_abort(void);

// Cancel bootloader rollback once the (possibly just-updated) app has proven
// itself functional. Call on reaching a healthy steady state; idempotent.
void ota_confirm(void);

// Cloud delivery journals the verified image BEFORE selecting its boot slot.
esp_err_t ota_prepare(void);
esp_err_t ota_activate(void);
esp_err_t ota_expect_version(const char *version);
// Start before NVS/SD/Wi-Fi initialization; confirm only after local services work.
esp_err_t ota_start_boot_guard(void);
