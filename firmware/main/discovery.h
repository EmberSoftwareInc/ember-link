// mDNS advertisement so Ember Bridge can find dongles without a network
// sweep: hostname ember-link-XXXX.local, service _ember-link._tcp:80
// with version/serial in TXT records.
#pragma once

#include "esp_err.h"

esp_err_t discovery_start(void);
