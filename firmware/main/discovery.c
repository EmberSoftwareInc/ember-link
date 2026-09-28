#include "discovery.h"

#include <stdio.h>

#include "esp_log.h"
#include "esp_mac.h"
#include "mdns.h"

#include "app_version.h"
#include "device_name.h"

static const char *TAG = "discovery";

esp_err_t discovery_start(void)
{
    esp_err_t err = mdns_init();
    if (err != ESP_OK) {
        return err;
    }

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char hostname[32], serial[13];
    snprintf(hostname, sizeof(hostname), "ember-link-%02x%02x", mac[4], mac[5]);
    snprintf(serial, sizeof(serial), "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    // The user's machine name ("Sewing room Brother") doubles as the mDNS
    // instance so browse results are recognizable without an extra HTTP call.
    static char device_name[DEVICE_NAME_MAX + 1];
    device_name_get(device_name);
    const char *instance = device_name[0] != '\0' ? device_name : EMBER_LINK_NAME;

    ESP_ERROR_CHECK(mdns_hostname_set(hostname));
    ESP_ERROR_CHECK(mdns_instance_name_set(instance));

    mdns_txt_item_t txt[] = {
        {"version", EMBER_LINK_VERSION},
        {"serial", serial},
        {"name", device_name},
    };
    ESP_ERROR_CHECK(mdns_service_add(instance, "_ember-link", "_tcp", 80,
                                     txt, sizeof(txt) / sizeof(txt[0])));

    ESP_LOGI(TAG, "advertising %s.local", hostname);
    return ESP_OK;
}
