#include "display.h"
#include "led.h"
#include "display_render.h"
#include "board.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/ledc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "display";
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static display_state_t s_state;
static display_settings_t s_settings;
static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }
#define LOCK() portENTER_CRITICAL(&s_lock)
#define UNLOCK() portEXIT_CRITICAL(&s_lock)
void display_card(display_card_t state) { LOCK(); if (s_state.card != state) s_state.changed_at = now_ms(); s_state.card = state; UNLOCK(); }
void display_wifi(display_wifi_t state) { LOCK(); if (s_state.wifi != state) s_state.changed_at = now_ms(); s_state.wifi = state; UNLOCK(); }
void display_cloud(display_cloud_t state) { LOCK(); if (s_state.cloud != state) s_state.changed_at = now_ms(); s_state.cloud = state; UNLOCK(); }
void display_begin(bool firmware, bool cloud, const char *name, uint64_t total) {
    LOCK(); display_state_begin(&s_state, firmware, cloud, name, total, now_ms()); UNLOCK();
}
void display_progress(uint64_t written) { LOCK(); s_state.done = written > s_state.total ? s_state.total : written; UNLOCK(); }
void display_finish(bool success, const char *error) { LOCK(); display_state_finish(&s_state, success, error, now_ms()); UNLOCK(); }
void display_notice(const char *text, bool error) { LOCK(); if (s_state.active) { UNLOCK(); return; } s_state.filename[0] = 0; s_state.firmware = false; display_state_notice(&s_state, text, error, 15000, now_ms()); UNLOCK(); }

display_settings_t display_get_settings(void) {
    LOCK(); display_settings_t settings = s_settings; UNLOCK(); return settings;
}
void display_apply_saved_settings(void) {
    display_settings_t settings; display_settings_load(&settings);
    LOCK(); s_settings=settings; s_state.changed_at=now_ms(); UNLOCK();
    led_set_enabled(settings.led_enabled);
}
esp_err_t display_configure(display_settings_t settings) {
    esp_err_t err = display_settings_save(settings);
    if (err == ESP_OK) display_apply_saved_settings();
    return err;
}

static uint16_t *pixels;
static spi_device_handle_t spi;
static spi_transaction_t transaction; // remains valid even if a timed-out DMA completes late
static bool pwm;
static esp_err_t send_bytes(const void *bytes, size_t len, bool data) {
    gpio_set_level(BOARD_LCD_DC_PIN, data);
    memset(&transaction,0,sizeof(transaction)); transaction.length=len*8;
    if(len<=4) { transaction.flags=SPI_TRANS_USE_TXDATA; memcpy(transaction.tx_data,bytes,len); }
    else transaction.tx_buffer=bytes;
    esp_err_t e=spi_device_queue_trans(spi,&transaction,pdMS_TO_TICKS(200));
    if(e!=ESP_OK) return e;
    spi_transaction_t *done=NULL;
    return spi_device_get_trans_result(spi,&done,pdMS_TO_TICKS(200));
}
static esp_err_t command(uint8_t cmd, const uint8_t *data, size_t count) {
    esp_err_t e=send_bytes(&cmd,1,false);
    return e==ESP_OK && count ? send_bytes(data,count,true) : e;
}
#define TRY(call) do { esp_err_t err_=(call); if(err_!=ESP_OK) return err_; } while(0)
static esp_err_t panel_init(void) {
    gpio_config_t gpio={.pin_bit_mask=(1ULL<<BOARD_LCD_DC_PIN)|(1ULL<<BOARD_LCD_RST_PIN)|(1ULL<<BOARD_LCD_BACKLIGHT_PIN),.mode=GPIO_MODE_OUTPUT};
    TRY(gpio_config(&gpio)); gpio_set_level(BOARD_LCD_BACKLIGHT_PIN,1);
    spi_bus_config_t bus={.mosi_io_num=BOARD_LCD_MOSI_PIN,.miso_io_num=-1,.sclk_io_num=BOARD_LCD_SCLK_PIN,.quadwp_io_num=-1,.quadhd_io_num=-1,.max_transfer_sz=160*80*2};
    TRY(spi_bus_initialize(SPI2_HOST,&bus,SPI_DMA_CH_AUTO));
    spi_device_interface_config_t dev={.clock_speed_hz=20000000,.mode=0,.spics_io_num=BOARD_LCD_CS_PIN,.queue_size=1};
    TRY(spi_bus_add_device(SPI2_HOST,&dev,&spi));
    gpio_set_level(BOARD_LCD_RST_PIN,0); vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(BOARD_LCD_RST_PIN,1); vTaskDelay(pdMS_TO_TICKS(150));
    TRY(command(0x01,NULL,0)); vTaskDelay(pdMS_TO_TICKS(150));
    TRY(command(0x11,NULL,0)); vTaskDelay(pdMS_TO_TICKS(120));
    // Panel register values from LilyGO's factory example (MIT; see THIRD_PARTY_NOTICES.md).
    static const struct { uint8_t cmd, len, data[16]; } init[]={
      {0xb1,3,{5,0x3a,0x3a}},{0xb2,3,{5,0x3a,0x3a}},{0xb3,6,{5,0x3a,0x3a,5,0x3a,0x3a}},
      {0xb4,1,{3}},{0xc0,3,{0x62,2,4}},{0xc1,1,{0xc0}},{0xc2,2,{0x0d,0}},
      {0xc3,2,{0x8d,0x6a}},{0xc4,2,{0x8d,0xee}},{0xc5,1,{0x0e}},{0x3a,1,{5}},
      {0xe0,16,{0x10,0x0e,2,3,0x0e,7,2,7,0x0a,0x12,0x27,0x37,0,0x0d,0x0e,0x10}},
      {0xe1,16,{0x10,0x0e,3,3,0x0f,6,2,8,0x0a,0x13,0x26,0x36,0,0x0d,0x0e,0x10}}
    };
    for(size_t i=0;i<sizeof(init)/sizeof(init[0]);i++) TRY(command(init[i].cmd,init[i].data,init[i].len));
    uint8_t madctl=0xa8; // worker applies the persisted orientation before lighting the panel
    TRY(command(0x36,&madctl,1)); TRY(command(0x21,NULL,0)); TRY(command(0x13,NULL,0));
    TRY(command(0x29,NULL,0));
    ledc_timer_config_t timer={.speed_mode=LEDC_LOW_SPEED_MODE,.duty_resolution=LEDC_TIMER_8_BIT,.timer_num=LEDC_TIMER_0,.freq_hz=1000,.clk_cfg=LEDC_AUTO_CLK};
    if(ledc_timer_config(&timer)==ESP_OK) {
        ledc_channel_config_t channel={.gpio_num=BOARD_LCD_BACKLIGHT_PIN,.speed_mode=LEDC_LOW_SPEED_MODE,.channel=LEDC_CHANNEL_0,.timer_sel=LEDC_TIMER_0,.duty=255};
        pwm=ledc_channel_config(&channel)==ESP_OK;
    }
    return ESP_OK;
}
static esp_err_t draw(const display_view_t *v) {
    display_render(pixels,v);
    static const uint8_t cols[]={0,1,0,160}, rows[]={0,26,0,105};
    TRY(command(0x2a,cols,4)); TRY(command(0x2b,rows,4)); TRY(command(0x2c,NULL,0));
    return send_bytes(pixels,160*80*2,true);
}
static void worker(void *arg) {
    (void)arg;
    pixels=heap_caps_malloc(160*80*2,MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL);
    esp_err_t err=pixels?panel_init():ESP_ERR_NO_MEM;
    display_view_t previous={0}; bool first=true, was_enabled=true;
    uint16_t rotation=UINT16_MAX;
    while(err==ESP_OK) {
        display_state_t snapshot; display_settings_t settings;
        LOCK(); snapshot=s_state; settings=s_settings; UNLOCK();
        if (!settings.enabled) {
            if (was_enabled) {
                if(pwm) ledc_stop(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_0,1);
                else gpio_set_level(BOARD_LCD_BACKLIGHT_PIN,1);
                err=command(0x28,NULL,0);
            }
            was_enabled=false; first=true;
            vTaskDelay(pdMS_TO_TICKS(250));
            continue;
        }
        if (rotation != settings.rotation) {
            uint8_t madctl=settings.rotation == 180 ? 0x68 : 0xa8;
            err=command(0x36,&madctl,1);
            if(err!=ESP_OK) break;
            rotation=settings.rotation; first=true;
        }
        wifi_ap_record_t ap; int rssi=esp_wifi_sta_get_ap_info(&ap)==ESP_OK?ap.rssi:0;
        display_view_t view; display_state_view(&snapshot,now_ms(),rssi,&view);
        if(first || memcmp(&previous,&view,sizeof(view))) {
            err=draw(&view);
            if(err!=ESP_OK) break;
            if(!was_enabled) { err=command(0x29,NULL,0); if(err!=ESP_OK) break; }
            was_enabled=true;
            if(pwm) { ledc_set_duty(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_0,view.dim?235:30); ledc_update_duty(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_0); }
            else gpio_set_level(BOARD_LCD_BACKLIGHT_PIN,0);
            previous=view; first=false;
        }
        vTaskDelay(pdMS_TO_TICKS(250)); // at most 4 FPS, even during large transfers
    }
    ESP_LOGW(TAG,"LCD disabled after error: %s; storage/network remain active",esp_err_to_name(err));
    if(pwm) ledc_stop(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_0,1);
    else gpio_set_level(BOARD_LCD_BACKLIGHT_PIN,1);
    // Do not free a DMA buffer/transaction that could still be owned by SPI.
    vTaskDelete(NULL);
}
void display_start(bool setup) {
    display_settings_t settings; display_settings_load(&settings);
    LOCK(); s_settings=settings; s_state.usb_setup=setup; s_state.changed_at=now_ms(); UNLOCK();
    led_set_enabled(settings.led_enabled);
    if(xTaskCreate(worker,"link_display",4096,NULL,1,NULL)!=pdPASS)
        ESP_LOGW(TAG,"LCD task unavailable; continuing without display");
}
