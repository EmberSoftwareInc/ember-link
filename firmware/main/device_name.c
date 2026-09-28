#include "device_name.h"

#include <string.h>

#include "nvs.h"

// Deliberately not wifi_mgr's namespace: re-provisioning WiFi must never
// touch the name, and each module owns its own factory-reset step.
#define NVS_NAMESPACE "emberid"
#define NVS_KEY "name"

void device_name_get(char out[DEVICE_NAME_MAX + 1])
{
    out[0] = '\0';
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) {
        return;
    }
    size_t len = DEVICE_NAME_MAX + 1;
    if (nvs_get_str(nvs, NVS_KEY, out, &len) != ESP_OK) {
        out[0] = '\0';
    }
    nvs_close(nvs);
}

esp_err_t device_name_set(const char *name)
{
    if (name == NULL || name[0] == '\0') {
        device_name_clear();
        return ESP_OK;
    }
    if (strlen(name) > DEVICE_NAME_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_str(nvs, NVS_KEY, name);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}

void device_name_clear(void)
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) != ESP_OK) {
        return;
    }
    nvs_erase_all(nvs);
    nvs_commit(nvs);
    nvs_close(nvs);
}
