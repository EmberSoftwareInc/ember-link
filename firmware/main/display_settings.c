#include "display_settings.h"
#include "nvs.h"
#include "sdkconfig.h"
// One versioned blob makes enabled/orientation a single committed preference.
display_settings_t display_settings_defaults(void)
{
#ifdef CONFIG_LINK_DISPLAY_FLIP
    return (display_settings_t){true, 180, true};
#else
    return (display_settings_t){true, 0, true};
#endif
}
void display_settings_load(display_settings_t *out)
{
    *out = display_settings_defaults();
    nvs_handle_t nvs;
    if (nvs_open("linkdisplay", NVS_READONLY, &nvs) != ESP_OK) return;
    uint8_t data[4]; size_t size = sizeof(data);
    esp_err_t err = nvs_get_blob(nvs, "prefs", data, &size);
    nvs_close(nvs);
    if (err != ESP_OK || size < 3 || data[1] > 1 || data[2] > 1) return;
    // Existing v1 preferences retain their screen choices and default LED on.
    if (size == 3 && data[0] == 1)
        *out = (display_settings_t){data[1] != 0, data[2] ? 180 : 0, true};
    else if (size == 4 && data[0] == 2 && data[3] <= 1)
        *out = (display_settings_t){data[1] != 0, data[2] ? 180 : 0, data[3] != 0};
}
esp_err_t display_settings_save(display_settings_t settings)
{
    if (settings.rotation != 0 && settings.rotation != 180) return ESP_ERR_INVALID_ARG;
    uint8_t data[] = {2, settings.enabled ? 1 : 0, settings.rotation == 180 ? 1 : 0, settings.led_enabled ? 1 : 0};
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("linkdisplay", NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(nvs, "prefs", data, sizeof(data));
    if (err == ESP_OK) err = nvs_commit(nvs);
    nvs_close(nvs);
    return err;
}
