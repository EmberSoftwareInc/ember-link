#pragma once
#include <stdbool.h>
#include <stdint.h>

// RTC-only session marker: never persists to flash or across a cold boot.
typedef struct { uint32_t magic, inverse; } usb_mode_latch_t;
void usb_mode_latch_request(usb_mode_latch_t *latch);
bool usb_mode_latch_boot(usb_mode_latch_t *latch, bool software_reset);

typedef enum { LINK_BUTTON_NONE, LINK_BUTTON_PAIR, LINK_BUTTON_USB_SETUP,
               LINK_BUTTON_RESET } link_button_action_t;
typedef struct {
    bool down, tap_pending;
    uint32_t down_at, released_at;
} link_button_t;
// Poll with monotonic milliseconds. Unsigned subtraction tolerates rollover.
// RESET repeats while held so the caller can retry when its operation gate frees.
link_button_action_t link_button_poll(link_button_t *state, bool pressed, uint32_t now);
