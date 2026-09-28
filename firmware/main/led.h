// Status LED (APA102) colour codes for the dongle's states.
#pragma once
#include <stdbool.h>

typedef enum {
    LED_OFF,
    LED_SETUP,      // blue: SoftAP provisioning mode, waiting for WiFi creds
    LED_CONNECTING, // yellow: joining WiFi
    LED_READY,      // green: on network, drive exposed to the machine
    LED_TRANSFER,   // cyan: receiving a design / writing to the card
    LED_UPDATE,     // magenta: firmware update in progress — do not unplug
    LED_ERROR,      // red: SD missing, WiFi failed, upload failed
} led_state_t;

void led_init(void);
void led_set(led_state_t state);
// Suppresses all states/blinks, while preserving the current logical status.
void led_set_enabled(bool enabled);
// Flash `state` on/off `times`, then restore the last led_set colour.
// Blocking (~300 ms per flash); call from low-priority tasks only.
void led_blink(led_state_t state, int times);
