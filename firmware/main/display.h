#pragma once
#include "display_state.h"
#include "display_settings.h"
// All publishing calls copy bounded data only; never wait for the LCD or a queue.
// Display startup/hardware failure is nonfatal to storage, networking and OTA.
void display_start(bool usb_setup);
void display_card(display_card_t state);
void display_wifi(display_wifi_t state);
void display_cloud(display_cloud_t state);
void display_begin(bool firmware, bool cloud, const char *filename, uint64_t total);
void display_progress(uint64_t written);
void display_finish(bool success, const char *error);
void display_notice(const char *text, bool error);

// Configuration writes NVS; call with the operation gate held.
// Apply the shared persisted state after a successful cloud transaction.
void display_apply_saved_settings(void);
display_settings_t display_get_settings(void);
esp_err_t display_configure(display_settings_t settings);
