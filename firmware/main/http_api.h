// REST API + provisioning page, port 80.
//
//   GET    /                    setup page (enter WiFi credentials)
//   GET    /api/health          {ok, name, version, serial}
//   GET    /api/info            identity + cached storage stats
//   GET    /api/files           cached file list
//   POST   /api/upload?filename=X   raw design bytes -> SD card -> USB replug
//   DELETE /api/files/X         remove a design -> USB replug
//   POST   /api/wifi            {ssid, password} -> save + reboot
//
// No authentication in v0: the API is only as private as the local network.
// TODO before shipping: pairing + per-bridge token like Ember Bridge's API.
#pragma once

#include "esp_err.h"

esp_err_t http_api_start(void);
