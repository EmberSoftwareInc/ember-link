// Desktop setup channel: a line-delimited JSON protocol over the CDC-ACM
// serial interface, spoken by Ember Bridge while the dongle is plugged into
// a computer. Physical USB attachment is the consent gesture — everything
// here is available without a bearer token.
//
// Requests are single JSON lines; an optional "id" is echoed in the
// response. Responses mirror the HTTP API's shape: {"id":1,"ok":true,...}
// or {"id":1,"ok":false,"error":{"code":"...","message":"..."}}.
//
//   {"cmd":"info"}                    → identity, provisioning + update state
//   {"cmd":"scan"}                    → nearby networks (dongle's own radio,
//                                       so only networks it can really join)
//   {"cmd":"provision","ssid":"X",
//    "password":"...","name":"..."}   → live trial; responds once the join
//                                       succeeds (credentials committed) or
//                                       fails (wrong_password / not_found /
//                                       timeout — nothing saved)
//   {"cmd":"set_name","name":"..."}   → rename without touching WiFi
//   {"cmd":"set_display","enabled":true,"rotation":180,"ledEnabled":false}
//                                      → persist/apply screen + LED settings; 0 or 180
//   {"cmd":"pair","name":"host"}      → mint a bearer token for the LAN API
//   {"cmd":"update","size":N}         → respond {"ready":true}, then stream
//                                       exactly N raw image bytes; signed-OTA
//                                       verify + reboot, progress events
//                                       {"event":"update","written":...}
//   {"cmd":"factory_reset"}           → wipe WiFi + tokens + name, reset display, reboot
#pragma once

#include "esp_err.h"

// Bring up the CDC interface and the command worker. Call after
// storage_init() — the TinyUSB driver must already be installed.
esp_err_t usb_setup_start(void);
