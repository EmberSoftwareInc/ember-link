#include "led.h"
#include "board.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned char frames[128][12];
static unsigned frame_count, bit_count, data_level, delays;
static bool locked, interrupt_blink;
void test_enter_critical(portMUX_TYPE *p) { (void)p; assert(!locked); locked=true; }
void test_exit_critical(portMUX_TYPE *p) { (void)p; assert(locked); locked=false; }
int gpio_config(const gpio_config_t *cfg) { assert(cfg->mode==GPIO_MODE_OUTPUT); return 0; }
int gpio_set_level(int pin, unsigned level) {
    assert(locked);
    if(pin==BOARD_LED_DATA_PIN) data_level=level;
    if(pin==BOARD_LED_CLK_PIN && level) {
        assert(frame_count<128);
        frames[frame_count][bit_count/8]=(frames[frame_count][bit_count/8]<<1)|data_level;
        if(++bit_count==96) { bit_count=0; frame_count++; }
    }
    return 0;
}
void esp_rom_delay_us(unsigned us) { assert(locked && us==1); }
void vTaskDelay(TickType_t ms) {
    assert(!locked && ms==150);
    if(interrupt_blink && ++delays==1) { led_set_enabled(false); led_set(LED_ERROR); }
}
static void color(unsigned index, unsigned r, unsigned g, unsigned b) {
    assert(frames[index][0]==0 && frames[index][4]==0xe1 && frames[index][11]==0xff);
    assert(frames[index][5]==b && frames[index][6]==g && frames[index][7]==r);
}
int main(void) {
    led_init(); color(frame_count-1,0,0,0);
    led_set(LED_READY); color(frame_count-1,0,255,0);
    led_set_enabled(false); color(frame_count-1,0,0,0);
    unsigned first=frame_count;
    led_set(LED_UPDATE); led_blink(LED_SETUP,2); led_set(LED_TRANSFER);
    for(unsigned i=first;i<frame_count;i++) color(i,0,0,0);
    led_set_enabled(true); color(frame_count-1,0,255,255);
    first=frame_count; interrupt_blink=true; led_blink(LED_UPDATE,2);
    color(first,255,0,255);
    for(unsigned i=first+1;i<frame_count;i++) color(i,0,0,0);
    led_set_enabled(true); color(frame_count-1,255,0,0);
    assert(!locked && bit_count==0);
    puts("LED tests passed (disabled states/blinks, mid-blink disable, current-state restore, frame locking)");
}
