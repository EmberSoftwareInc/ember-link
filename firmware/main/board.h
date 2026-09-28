// LilyGo T-Dongle-S3 pin map.
// Source: Xinyuan-LilyGO/T-Dongle-S3 examples/factory_screen/factory_screen.ino
#pragma once

#define BOARD_BUTTON_PIN 0 // BOOT button, active low

// APA102 status LED (single pixel, bit-banged)
#define BOARD_LED_DATA_PIN 40
#define BOARD_LED_CLK_PIN 39

// microSD card, 4-bit SDMMC mode
#define BOARD_SD_CLK_PIN 12
#define BOARD_SD_CMD_PIN 16
#define BOARD_SD_D0_PIN 14
#define BOARD_SD_D1_PIN 17
#define BOARD_SD_D2_PIN 21
#define BOARD_SD_D3_PIN 18

// ST7735 LCD, 160x80, SPI2. Backlight is ACTIVE LOW.
// Driven by the optional, isolated status-display task.
#define BOARD_LCD_MOSI_PIN 3
#define BOARD_LCD_SCLK_PIN 5
#define BOARD_LCD_CS_PIN 4
#define BOARD_LCD_DC_PIN 2
#define BOARD_LCD_RST_PIN 1
#define BOARD_LCD_BACKLIGHT_PIN 38
