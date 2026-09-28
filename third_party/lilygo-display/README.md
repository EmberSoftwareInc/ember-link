The ST7735 initialization register values in `firmware/main/display.c` are
adapted from LilyGO's T-Dongle-S3 factory example, under the MIT license here:
https://github.com/Xinyuan-LilyGO/T-Dongle-S3/blob/bb7654607d280fc2a1451d24abf6ed027287d416/examples/factory_screen/esp_lcd_st7735.c
The landscape offsets, pins, active-low backlight and orientation follow the
adjacent `factory_screen.ino`. The bounded SPI transport, renderer, glyphs and
status model are implemented in this repository; no GUI framework is required.
