#pragma once
#include "esp_err.h"
// The v1 config/receipt blobs remain frozen and authoritative for old images.
// New settings live in a separate, versioned extension record.
esp_err_t cloud_settings_migrate(void);
#define LINK_SETTINGS_SCHEMA 2
