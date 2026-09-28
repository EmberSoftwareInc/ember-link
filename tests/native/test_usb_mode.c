#include <assert.h>
#include <stdio.h>
#include "usb_mode_state.h"

static void test_session(void)
{
    usb_mode_latch_t latch = {0};
    assert(!usb_mode_latch_boot(&latch, false));
    assert(!usb_mode_latch_boot(&latch, true));
    usb_mode_latch_request(&latch);
    assert(usb_mode_latch_boot(&latch, true));
    assert(usb_mode_latch_boot(&latch, true)); // software reboot during setup
    assert(!usb_mode_latch_boot(&latch, false)); // power loss/other reset
    assert(!usb_mode_latch_boot(&latch, true)); // stale marker cannot reappear
    usb_mode_latch_request(&latch);
    latch.inverse ^= 1;
    assert(!usb_mode_latch_boot(&latch, true));
    assert(latch.magic == 0 && latch.inverse == 0);
}

static void test_gestures(uint32_t base)
{
    link_button_t b = {0};
    // Bounce is ignored; a single click fires only after the double-click window.
    assert(link_button_poll(&b, true, base) == LINK_BUTTON_NONE);
    assert(link_button_poll(&b, false, base+20) == LINK_BUTTON_NONE);
    assert(link_button_poll(&b, false, base+700) == LINK_BUTTON_NONE);
    assert(link_button_poll(&b, true, base+1000) == LINK_BUTTON_NONE);
    assert(link_button_poll(&b, false, base+1150) == LINK_BUTTON_NONE);
    assert(link_button_poll(&b, false, base+2150) == LINK_BUTTON_NONE);
    assert(link_button_poll(&b, false, base+2200) == LINK_BUTTON_PAIR);
    assert(link_button_poll(&b, false, base+2300) == LINK_BUTTON_NONE);
    // A double press emits setup, never a pairing action first.
    assert(link_button_poll(&b, true, base+2400) == LINK_BUTTON_NONE);
    assert(link_button_poll(&b, false, base+2450) == LINK_BUTTON_NONE);
    assert(link_button_poll(&b, true, base+2550) == LINK_BUTTON_NONE);
    assert(link_button_poll(&b, false, base+2600) == LINK_BUTTON_USB_SETUP);
    assert(link_button_poll(&b, false, base+2700) == LINK_BUTTON_NONE);
    // Inclusive 1 s boundary is measured at the second press, not its release.
    assert(link_button_poll(&b, true, base+2800) == LINK_BUTTON_NONE);
    assert(link_button_poll(&b, false, base+2850) == LINK_BUTTON_NONE);
    assert(link_button_poll(&b, true, base+2900) == LINK_BUTTON_NONE);
    assert(link_button_poll(&b, false, base+2920) == LINK_BUTTON_NONE); // bounce
    assert(link_button_poll(&b, true, base+3850) == LINK_BUTTON_NONE);
    assert(link_button_poll(&b, false, base+3900) == LINK_BUTTON_USB_SETUP);
    // A long second press cancels a pending click, with retryable reset action.
    assert(link_button_poll(&b, true, base+4000) == LINK_BUTTON_NONE);
    assert(link_button_poll(&b, false, base+4150) == LINK_BUTTON_NONE);
    assert(link_button_poll(&b, true, base+4250) == LINK_BUTTON_NONE);
    assert(link_button_poll(&b, true, base+9200) == LINK_BUTTON_NONE);
    assert(link_button_poll(&b, true, base+9250) == LINK_BUTTON_RESET);
    assert(link_button_poll(&b, true, base+9300) == LINK_BUTTON_RESET);
    assert(link_button_poll(&b, false, base+9400) == LINK_BUTTON_NONE);
    assert(link_button_poll(&b, false, base+10000) == LINK_BUTTON_NONE);
    // A 2–5 second press performs neither reset nor setup.
    assert(link_button_poll(&b, true, base+11000) == LINK_BUTTON_NONE);
    assert(link_button_poll(&b, false, base+14000) == LINK_BUTTON_NONE);
    assert(link_button_poll(&b, false, base+15000) == LINK_BUTTON_NONE);
}

static void test_slow_second_press(void)
{
    link_button_t b = {0};
    link_button_poll(&b, true, 100);
    link_button_poll(&b, false, 250);
    link_button_poll(&b, true, 1300);
    assert(link_button_poll(&b, false, 1500) == LINK_BUTTON_PAIR);
    assert(link_button_poll(&b, false, 2550) == LINK_BUTTON_PAIR);
    assert(link_button_poll(&b, false, 2600) == LINK_BUTTON_NONE);
}

int main(void)
{
    test_session();
    test_gestures(0);
    test_gestures(UINT32_MAX - 2200u); // click/release window across timer rollover
    test_slow_second_press();
    puts("USB mode/session and button gesture tests passed");
}
