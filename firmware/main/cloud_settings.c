#include "cloud_settings.h"
#include "nvs.h"
#include "cJSON.h"
#include <string.h>

esp_err_t cloud_settings_migrate(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("link_cloud", NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;
    char data[256] = {0};
    size_t size = sizeof(data);
    err = nvs_get_blob(nvs, "settings_v2", data, &size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        // Additive migration: never rewrite Wi-Fi, identity, config or receipts.
        const char initial[] = "{\"schema\":2,\"transport\":\"poll\"}";
        err = nvs_set_blob(nvs, "settings_v2", initial, sizeof(initial));
        if (err == ESP_OK) err = nvs_commit(nvs);
    } else if (err == ESP_OK) {
        cJSON *o = size && size <= sizeof(data) && data[size-1] == 0
            ? cJSON_ParseWithLengthOpts(data, size, NULL, true) : NULL;
        const cJSON *schema = cJSON_GetObjectItemCaseSensitive(o, "schema");
        const cJSON *transport = cJSON_GetObjectItemCaseSensitive(o, "transport");
        // Do not guess at a future schema or erase data to make boot succeed.
        if (!cJSON_IsNumber(schema) || schema->valuedouble != LINK_SETTINGS_SCHEMA ||
            !cJSON_IsString(transport) || strcmp(transport->valuestring, "poll"))
            err = ESP_ERR_INVALID_STATE;
        cJSON_Delete(o);
    }
    nvs_close(nvs);
    return err;
}
