#pragma once
#include <stdbool.h>
#include "cJSON.h"
#include "esp_err.h"

esp_err_t cloud_init(void);
esp_err_t cloud_start(void);
// Factory/developer USB provisioning. Never returns credentials. No LAN route.
esp_err_t cloud_configure(const cJSON *request);
// Consumer setup: forward a short-lived USB proof using the private cloud token.
// Identity must already have been provisioned during manufacture.
esp_err_t cloud_claim(const cJSON *request);
esp_err_t cloud_set_enabled(bool enabled);
// May return {busy:true} while a network operation holds the session.
cJSON *cloud_status(void);
// Clears enablement only, retaining identity and unsettled transfer receipts.
esp_err_t cloud_disable_for_reset(void);
