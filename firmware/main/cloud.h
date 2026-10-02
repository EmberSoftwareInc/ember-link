#pragma once
#include <stdbool.h>
#include "cJSON.h"
#include "esp_err.h"

esp_err_t cloud_init(void);
esp_err_t cloud_start(void);
// Factory/developer USB provisioning. Never returns credentials. No LAN route.
esp_err_t cloud_configure(const cJSON *request);
// Consumer setup: forward a short-lived USB proof using the private cloud token.
// Identity must already be configured (factory or self-service enrollment).
esp_err_t cloud_claim(const cJSON *request);
// USB-only first-time enrollment. serial is read from hardware, not the request.
// Returns after durable staging; completion is asynchronous in cloud_status.
typedef enum {
    CLOUD_ENROLL_OK,
    CLOUD_ENROLL_INVALID_REQUEST,
    CLOUD_ENROLL_BUSY,
    CLOUD_ENROLL_ALREADY_CONFIGURED,
    CLOUD_ENROLL_SERVICE_MISMATCH,
    CLOUD_ENROLL_STORAGE_ERROR,
} cloud_enroll_result_t;
cloud_enroll_result_t cloud_enroll(const cJSON *request, const char *serial);
const char *cloud_enroll_error_code(cloud_enroll_result_t result);
const char *cloud_enroll_error_message(cloud_enroll_result_t result);
esp_err_t cloud_set_enabled(bool enabled);
// While busy, returns a redacted cached snapshot with cached:true and snapshotAgeMs.
// Receipts/update details are only included in a fresh (cached:false) response.
cJSON *cloud_status(void);
// Clears enablement only, retaining identity and unsettled transfer receipts.
esp_err_t cloud_disable_for_reset(void);
