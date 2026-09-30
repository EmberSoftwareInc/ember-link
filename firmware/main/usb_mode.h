#pragma once
#include <stdbool.h>

void usb_mode_init(void);
bool usb_mode_is_setup(void);
const char *usb_mode_name(void);
// Caller must hold the operation gate and wait for the button to be released.
void usb_mode_enter_setup(void);

bool usb_mode_is_card_maintenance(void);
void usb_mode_enter_card_maintenance(void);
