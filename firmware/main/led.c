// APA102 status LED, bit-banged. One pixel; timing is forgiving (clocked
// protocol, no tight timing like WS2812), so plain GPIO writes are fine.
#include "led.h"

#include "board.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static led_state_t s_current = LED_OFF;
static bool s_enabled = true;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

#define LED_BRIGHTNESS 1 // 0..31; the APA102 is blinding at full power

static void shift_byte(uint8_t byte)
{
    for (int bit = 7; bit >= 0; bit--) {
        gpio_set_level(BOARD_LED_DATA_PIN, (byte >> bit) & 1);
        esp_rom_delay_us(1);
        gpio_set_level(BOARD_LED_CLK_PIN, 1);
        esp_rom_delay_us(1);
        gpio_set_level(BOARD_LED_CLK_PIN, 0);
    }
}

static void show(uint8_t r, uint8_t g, uint8_t b)
{
    for (int i = 0; i < 4; i++) {
        shift_byte(0x00); // start frame
    }
    shift_byte(0xE0 | LED_BRIGHTNESS);
    shift_byte(b);
    shift_byte(g);
    shift_byte(r);
    for (int i = 0; i < 4; i++) {
        shift_byte(0xFF); // end frame
    }
}

void led_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << BOARD_LED_DATA_PIN) | (1ULL << BOARD_LED_CLK_PIN),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&cfg);
    led_set(LED_OFF);
}

static void show_state(led_state_t state)
{
    switch (state) {
    case LED_OFF:
        show(0, 0, 0);
        break;
    case LED_SETUP:
        show(0, 0, 255);
        break;
    case LED_CONNECTING:
        show(255, 180, 0);
        break;
    case LED_READY:
        show(0, 255, 0);
        break;
    case LED_TRANSFER:
        show(0, 255, 255);
        break;
    case LED_UPDATE:
        show(255, 0, 255);
        break;
    case LED_ERROR:
        show(255, 0, 0);
        break;
    }
}

void led_set(led_state_t state)
{
    portENTER_CRITICAL(&s_lock);
    s_current = state;
    show_state(s_enabled ? state : LED_OFF);
    portEXIT_CRITICAL(&s_lock);
}

void led_set_enabled(bool enabled)
{
    portENTER_CRITICAL(&s_lock);
    s_enabled = enabled;
    show_state(enabled ? s_current : LED_OFF);
    portEXIT_CRITICAL(&s_lock);
}

static void blink_frame(led_state_t state, bool restore)
{
    portENTER_CRITICAL(&s_lock);
    show_state(s_enabled ? (restore ? s_current : state) : LED_OFF);
    portEXIT_CRITICAL(&s_lock);
}

void led_blink(led_state_t state, int times)
{
    for (int i = 0; i < times; i++) {
        blink_frame(state, false);
        vTaskDelay(pdMS_TO_TICKS(150));
        blink_frame(LED_OFF, false);
        vTaskDelay(pdMS_TO_TICKS(150));
    }
    blink_frame(LED_OFF, true);
}
