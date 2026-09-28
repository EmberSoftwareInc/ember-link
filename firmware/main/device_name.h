// The user-visible name of the machine this dongle lives in ("Sewing room
// Brother"), chosen during setup. Stored in NVS; empty until the user picks
// one, in which case UIs fall back to the serial-derived default.
#pragma once

#include "esp_err.h"

#define DEVICE_NAME_MAX 48 // bytes, excluding the terminator

// Copies the stored name ("" if unset). `out` must hold DEVICE_NAME_MAX + 1.
void device_name_get(char out[DEVICE_NAME_MAX + 1]);

// Persist a new name. NULL or "" clears it back to unset.
// ESP_ERR_INVALID_ARG if longer than DEVICE_NAME_MAX bytes.
esp_err_t device_name_set(const char *name);

// Factory reset: forget the name.
void device_name_clear(void);
