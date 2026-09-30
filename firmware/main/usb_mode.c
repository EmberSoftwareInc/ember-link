#include "usb_mode.h"
#include "usb_mode_state.h"
#include "esp_attr.h"
#include "esp_system.h"

static RTC_NOINIT_ATTR usb_mode_latch_t s_latch;
static bool s_setup;
static RTC_NOINIT_ATTR usb_mode_latch_t s_card_latch;
static bool s_card_maintenance;

void usb_mode_init(void)
{
    // An explicit double press enables the session. Software reboots (including
    // updates) preserve it; power-on, brownout, watchdog and other resets clear it.
    s_setup = usb_mode_latch_boot(&s_latch, esp_reset_reason() == ESP_RST_SW);
    s_card_maintenance = usb_mode_latch_boot(&s_card_latch, esp_reset_reason() == ESP_RST_SW);
    s_setup |= s_card_maintenance;
}

bool usb_mode_is_setup(void) { return s_setup; }
const char *usb_mode_name(void) { return s_setup ? "setup" : "storage"; }

void usb_mode_enter_setup(void)
{
    usb_mode_latch_request(&s_latch);
    esp_restart();
}

bool usb_mode_is_card_maintenance(void) { return s_card_maintenance; }
void usb_mode_enter_card_maintenance(void)
{
    usb_mode_latch_request(&s_card_latch);
    usb_mode_enter_setup();
}
