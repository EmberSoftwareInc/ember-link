#pragma once
#include "display_state.h"
// Render an RGB565 frame in SPI byte order into exactly 160*80 pixels.
void display_render(uint16_t pixels[160*80], const display_view_t *view);
