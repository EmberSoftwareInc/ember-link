#include "display.h"
// Ember Link — WiFi "memory stick" dongle for embroidery machines.
//
// The dongle sits in the machine's USB port pretending to be a FAT flash
// drive (backed by the hidden microSD card). Ember Bridge sends designs to
// it over WiFi; after each write the dongle re-plugs itself so the machine
// picks up the new file.
//
// Boot sequence: SD card -> USB mass storage -> WiFi (station with stored
// credentials, else provisioning SoftAP) -> HTTP API + mDNS.
#include "esp_log.h"
#include "esp_system.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "auth.h"
#include "cloud.h"
#include "operation.h"
#include "psa/crypto.h"
#include "board.h"
#include "device_name.h"
#include "discovery.h"
#include "dns_hijack.h"
#include "http_api.h"
#include "led.h"
#include "ota.h"
#include "storage.h"
#include "usb_setup.h"
#include "usb_mode.h"
#include "usb_mode_state.h"
#include "wifi_mgr.h"

static const char *TAG = "main";


// Tracked separately: USB provisioning can move the dongle from setup mode
// (http up, no mDNS) straight to connected without a reboot, and discovery
// must still start on that first connect.
static bool s_http_started;
static bool s_discovery_started;

static void on_waiting_for_card(void)
{
    display_card(DISPLAY_CARD_MISSING);
    led_set(LED_ERROR);
}

static void on_wifi_state(wifi_mgr_state_t state)
{
    switch (state) {
    case WIFI_MGR_CONNECTING:
        display_wifi(DISPLAY_WIFI_CONNECTING);
        led_set(LED_CONNECTING);
        break;
    case WIFI_MGR_CONNECTED:
        display_wifi(DISPLAY_WIFI_READY);
        dns_hijack_stop();
        if (!s_http_started) {
            s_http_started = true;
            ESP_ERROR_CHECK(http_api_start());
        }
        if (!s_discovery_started) {
            s_discovery_started = true;
            ESP_ERROR_CHECK(discovery_start());
        }
        led_set(LED_READY);
        // Reaching the network with services up is our post-update health
        // check: cancel the bootloader rollback for this image.
        ota_confirm();
        break;
    case WIFI_MGR_AP_MODE:
        display_wifi(DISPLAY_WIFI_SETUP);
        if (!s_http_started) {
            s_http_started = true;
            ESP_ERROR_CHECK(http_api_start()); // serves the setup page
        }
        dns_hijack_start(); // captive portal: pop the sign-in sheet
        led_set(LED_SETUP);
        ota_confirm(); // setup mode working = the firmware itself is fine
        break;
    }
}

// One press opens local pairing; two quick presses enable USB setup on a
// computer; a five-second hold resets settings. USB setup never changes Wi-Fi
// or account state and returns to storage-only on the next power cycle.
static void button_task(void *arg)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << BOARD_BUTTON_PIN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&cfg);
    link_button_t button = {0};
    bool setup_requested = false;
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(10));
        bool pressed = gpio_get_level(BOARD_BUTTON_PIN) == 0;
        link_button_action_t action = link_button_poll(
            &button, pressed, (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS));
        if (action == LINK_BUTTON_RESET) {
            setup_requested = false;
            if (!operation_begin()) continue;
            if (cloud_disable_for_reset() != ESP_OK) { operation_end(); continue; }
            ESP_LOGW(TAG, "BOOT held: factory reset");
            led_set(LED_SETUP);
            wifi_mgr_clear_credentials();
            auth_clear_all();
            device_name_clear();
            (void)display_configure(display_settings_defaults());
            vTaskDelay(pdMS_TO_TICKS(500));
            esp_restart();
        } else if (action == LINK_BUTTON_USB_SETUP) {
            setup_requested = !usb_mode_is_setup();
        } else if (action == LINK_BUTTON_PAIR) {
            auth_open_window();
            display_notice("Ready to pair", false);
            led_blink(LED_UPDATE, 3);
        }
        // A busy upload/update must finish before the mode-switch reboot.
        // Retain the physical request and retry; never abandon an SD write.
        if (setup_requested && !pressed && operation_begin()) {
            ESP_LOGI(TAG, "entering USB setup for this powered session");
            led_blink(LED_SETUP, 3);
            if (gpio_get_level(BOARD_BUTTON_PIN) == 0) {
                operation_end();
                continue; // button was pressed again during the acknowledgement
            }
            usb_mode_enter_setup();
        }
    }
}

void app_main(void)
{
    usb_mode_init();
    ESP_ERROR_CHECK(ota_start_boot_guard());
    // Never erase factory identity or transfer receipts on an NVS error.
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(operation_init());
    ESP_ERROR_CHECK(display_settings_init());
    ESP_ERROR_CHECK(cloud_init());
    ESP_ERROR_CHECK(psa_crypto_init() == PSA_SUCCESS ? ESP_OK : ESP_FAIL);

    led_init();
    display_start(usb_mode_is_setup());
    led_set(LED_CONNECTING);

    ESP_ERROR_CHECK(auth_init());
    esp_err_t storage_result = storage_init(wifi_mgr_has_credentials(), usb_mode_is_setup(), on_waiting_for_card);
    if (storage_result != ESP_OK) {
        display_card(DISPLAY_CARD_ERROR);
        vTaskDelay(pdMS_TO_TICKS(2000)); // allow the card guidance to become visible
        ESP_ERROR_CHECK(storage_result);
    }
    display_card(DISPLAY_CARD_READY);
    if (usb_mode_is_setup()) ESP_ERROR_CHECK(usb_setup_start());
    ESP_ERROR_CHECK(wifi_mgr_start(on_wifi_state));

    ESP_ERROR_CHECK(cloud_start());

    xTaskCreate(button_task, "button", 3072, NULL, 5, NULL);

    ESP_LOGI(TAG, "Ember Link up (USB %s)", usb_mode_name());
}
