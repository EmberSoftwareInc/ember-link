// WiFi connection manager.
//
// With stored credentials: connect as a station and retry forever on
// non-auth failures (a headless dongle must survive router reboots). If the
// join fails with an authentication error a few times in a row — almost
// always a wrong password — fall back to setup mode at runtime, keeping the
// credentials, and expose the failure so the setup page can explain itself.
//
// Setup mode is an open SoftAP ("Ember Link-XXXX") in APSTA mode: AP for
// the setup page (with captive-portal DNS, see dns_hijack), STA so the
// dongle can scan for nearby networks to offer as a picklist.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

typedef enum {
    WIFI_MGR_CONNECTING, // trying to join the stored network
    WIFI_MGR_CONNECTED,  // got an IP
    WIFI_MGR_AP_MODE,    // setup SoftAP is up
} wifi_mgr_state_t;

typedef void (*wifi_mgr_cb_t)(wifi_mgr_state_t state);

typedef enum {
    WIFI_PROVISION_OK,          // joined and got an IP; credentials saved
    WIFI_PROVISION_AUTH_FAILED, // the network rejected us — wrong password
    WIFI_PROVISION_NOT_FOUND,   // no network with that SSID in range
    WIFI_PROVISION_TIMEOUT,     // network never yielded an IP
} wifi_provision_result_t;

/// `ip` is the dotted-quad station address on WIFI_PROVISION_OK, NULL otherwise.
typedef void (*wifi_provision_cb_t)(wifi_provision_result_t result, const char *ip);

typedef struct {
    char ssid[33];
    int rssi;
    bool secure;
} wifi_scan_result_t;

esp_err_t wifi_mgr_start(wifi_mgr_cb_t cb);

bool wifi_mgr_has_credentials(void);
esp_err_t wifi_mgr_save_credentials(const char *ssid, const char *password);
esp_err_t wifi_mgr_clear_credentials(void);

/// Try credentials live and only commit them on success — the desktop setup
/// flow's validate-before-save. Works from any state: in setup mode the trial
/// runs on the idle STA interface (the SoftAP stays up); when already joined
/// to a network the dongle hops to the new one and hops back on failure.
/// On WIFI_PROVISION_OK the credentials are saved, the dongle stays on the
/// new network, and a leftover setup AP is torn down. No reboot involved.
/// The callback fires once, from the WiFi event loop (or the timeout timer) —
/// don't block in it. ESP_ERR_INVALID_STATE if an attempt is already running.
esp_err_t wifi_mgr_provision(const char *ssid, const char *password, wifi_provision_cb_t cb);

/// True while the setup SoftAP is the active mode (first boot or fallback).
bool wifi_mgr_in_setup(void);

/// Scan for nearby networks (blocking, ~2 s). Results are deduped by SSID
/// (strongest kept) and sorted by signal. Returns the number written.
size_t wifi_mgr_scan(wifi_scan_result_t *out, size_t max);

/// Why the last join attempt gave up, e.g. `couldn't join "X": wrong
/// password?` — empty string if the last attempt didn't fail that way.
void wifi_mgr_last_error(char out[96]);

/// SSID of the stored credentials ("" if none).
void wifi_mgr_configured_ssid(char out[33]);

// Current station IP as dotted quad ("0.0.0.0" when not connected).
void wifi_mgr_get_ip(char out[16]);
// SoftAP SSID, e.g. "Ember Link-3F2A".
void wifi_mgr_get_ap_ssid(char out[33]);
