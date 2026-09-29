#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "link_protocol.h"
#define DISPLAY_REVISION_MAX 9007199254740991ULL

typedef struct {
    bool enabled;
    uint16_t rotation;
    bool led_enabled;
} display_settings_t;
typedef enum {
    DISPLAY_RECEIPT_NONE,
    DISPLAY_RECEIPT_APPLIED,
    DISPLAY_RECEIPT_CONFLICT
} display_receipt_state_t;
typedef struct {
    char command_id[LINK_MAX_ID];
    uint64_t ownership_generation, expected_revision, revision;
    display_settings_t settings; // Snapshot at decision time; USB may subsequently change it.
    display_receipt_state_t state;
    bool acknowledged;
} display_receipt_t;
typedef struct {
    display_settings_t settings;
    uint64_t revision;
    display_receipt_t receipt;
} display_settings_snapshot_t;

display_settings_t display_settings_defaults(void);
// Initialize before tasks start. Corrupt/unknown journals fail closed, never reset revision.
esp_err_t display_settings_init(void);
display_settings_snapshot_t display_settings_snapshot(void);
void display_settings_load(display_settings_t *out);
// All mutations below require the shared operation gate. Readers are thread-safe.
// Local saves (including no-op/reset) advance revision and retain any cloud receipt.
esp_err_t display_settings_save(display_settings_t settings);
esp_err_t display_settings_apply(const char *command_id, uint64_t generation,
                                 uint64_t expected_revision, display_settings_t settings);
esp_err_t display_settings_ack(const display_receipt_t *receipt);
bool display_settings_pending(void);
bool display_settings_available(void);
