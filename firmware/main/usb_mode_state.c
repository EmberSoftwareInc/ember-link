#include "usb_mode_state.h"

#define SETUP_MAGIC UINT32_C(0x4c555342)
#define TAP_MIN_MS 40u
#define TAP_MAX_MS 2000u
#define DOUBLE_TAP_MS 1000u
#define RESET_HOLD_MS 5000u

void usb_mode_latch_request(usb_mode_latch_t *latch)
{
    latch->magic = SETUP_MAGIC;
    latch->inverse = ~SETUP_MAGIC;
}

bool usb_mode_latch_boot(usb_mode_latch_t *latch, bool software_reset)
{
    bool setup = software_reset && latch->magic == SETUP_MAGIC &&
                 latch->inverse == ~SETUP_MAGIC;
    if (!setup) *latch = (usb_mode_latch_t){0};
    return setup;
}

link_button_action_t link_button_poll(link_button_t *s, bool pressed, uint32_t now)
{
    if (pressed) {
        if (!s->down) { s->down = true; s->down_at = now; }
        if ((uint32_t)(now - s->down_at) >= RESET_HOLD_MS) {
            s->tap_pending = false;
            return LINK_BUTTON_RESET;
        }
        return LINK_BUTTON_NONE;
    }
    if (s->down) {
        s->down = false;
        uint32_t held = now - s->down_at;
        if (held >= TAP_MIN_MS && held < TAP_MAX_MS) {
            if (s->tap_pending && (uint32_t)(s->down_at - s->released_at) <= DOUBLE_TAP_MS) {
                s->tap_pending = false;
                return LINK_BUTTON_USB_SETUP;
            }
            bool previous = s->tap_pending;
            s->tap_pending = true;
            s->released_at = now;
            return previous ? LINK_BUTTON_PAIR : LINK_BUTTON_NONE;
        }
        if (held >= TAP_MAX_MS) s->tap_pending = false;
    }
    if (s->tap_pending && (uint32_t)(now - s->released_at) > DOUBLE_TAP_MS) {
        s->tap_pending = false;
        return LINK_BUTTON_PAIR;
    }
    return LINK_BUTTON_NONE;
}
