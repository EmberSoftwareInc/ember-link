// Atomic NVS fixture with an independently preserved legacy record.
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include <assert.h>
#include <string.h>
static unsigned char settings_disk[112], settings_pending[112], settings_legacy[4];
static size_t settings_size, settings_legacy_size;
static bool settings_fail_write, settings_fail_commit, settings_commit_ambiguous;
static unsigned settings_writes;
void test_enter_critical(portMUX_TYPE *p)
{
    assert(!*p);
    ++*p;
}
void test_exit_critical(portMUX_TYPE *p)
{
    assert(*p == 1);
    --*p;
}
static esp_err_t settings_read(const char *key, void *out, size_t *size)
{
    bool legacy = !strcmp(key, "prefs");
    assert(legacy || !strcmp(key, "state_v1"));
    size_t count = legacy ? settings_legacy_size : settings_size;
    if (!count)
        return ESP_ERR_NVS_NOT_FOUND;
    if (*size < count)
        return ESP_ERR_INVALID_SIZE;
    memcpy(out, legacy ? settings_legacy : settings_disk, count);
    *size = count;
    return ESP_OK;
}
static esp_err_t settings_write(const char *key, const void *data, size_t size)
{
    assert(!strcmp(key, "state_v1") && size == 112);
    if (settings_fail_write)
        return ESP_FAIL;
    memcpy(settings_pending, data, size);
    ++settings_writes;
    return ESP_OK;
}
static esp_err_t settings_commit(void)
{
    if (settings_fail_commit && !settings_commit_ambiguous)
        return ESP_FAIL;
    memcpy(settings_disk, settings_pending, 112);
    settings_size = 112;
    return settings_fail_commit ? ESP_FAIL : ESP_OK;
}
