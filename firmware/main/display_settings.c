#include "display_settings.h"
#include "nvs.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include <string.h>
#include <stdatomic.h>

// Explicit, fixed-width encoding; independent of compiler padding and enum size.
// Legacy prefs remain untouched so a rollback can still read its original choices.
#define RECORD_SIZE 112
_Static_assert(LINK_MAX_ID == 64, "Changing ID width requires a journal schema migration");
static display_settings_snapshot_t s_current;
static atomic_bool s_ready;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static bool valid(display_settings_t s)
{
    return s.rotation == 0 || s.rotation == 180;
}
static void put64(uint8_t *p, uint64_t v)
{
    for (unsigned i = 0; i < 8; i++)
        p[i] = (uint8_t)(v >> (i * 8));
}
static uint64_t get64(const uint8_t *p)
{
    uint64_t v = 0;
    for (unsigned i = 0; i < 8; i++)
        v |= (uint64_t)p[i] << (i * 8);
    return v;
}
static void publish(display_settings_snapshot_t next)
{
    portENTER_CRITICAL(&s_lock);
    s_current = next;
    portEXIT_CRITICAL(&s_lock);
}
display_settings_snapshot_t display_settings_snapshot(void)
{
    portENTER_CRITICAL(&s_lock);
    display_settings_snapshot_t value = s_current;
    portEXIT_CRITICAL(&s_lock);
    return value;
}
display_settings_t display_settings_defaults(void)
{
#ifdef CONFIG_LINK_DISPLAY_FLIP
    return (display_settings_t){true, 180, true};
#else
    return (display_settings_t){true, 0, true};
#endif
}
static esp_err_t persist(display_settings_snapshot_t next)
{
    uint8_t data[RECORD_SIZE] = {0};
    memcpy(data, "LDS1", 4);
    data[4] = next.settings.enabled;
    data[5] = next.settings.rotation == 180;
    data[6] = next.settings.led_enabled;
    data[7] = (uint8_t)next.receipt.state;
    put64(data + 8, next.revision);
    data[16] = next.receipt.acknowledged;
    data[17] = next.receipt.settings.enabled;
    data[18] = next.receipt.settings.rotation == 180;
    data[19] = next.receipt.settings.led_enabled;
    memcpy(data + 24, next.receipt.command_id, LINK_MAX_ID);
    put64(data + 88, next.receipt.ownership_generation);
    put64(data + 96, next.receipt.expected_revision);
    put64(data + 104, next.receipt.revision);
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("linkdisplay", NVS_READWRITE, &nvs);
    if (err != ESP_OK)
        return err;
    err = nvs_set_blob(nvs, "state_v1", data, sizeof(data));
    if (err == ESP_OK)
        err = nvs_commit(nvs);
    nvs_close(nvs);
    if (err == ESP_OK)
        publish(next);
    else
        s_ready = false; // A failed commit can be ambiguous; only reboot/reload may resume writes.
    return err;
}
esp_err_t display_settings_init(void)
{
    s_ready = false;
    display_settings_snapshot_t next = {.settings = display_settings_defaults()};
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("linkdisplay", NVS_READONLY, &nvs);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        publish(next);
        s_ready = true;
        return ESP_OK;
    }
    if (err != ESP_OK)
        return err;
    uint8_t data[RECORD_SIZE] = {0};
    size_t size = sizeof(data);
    err = nvs_get_blob(nvs, "state_v1", data, &size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        uint8_t legacy[4];
        size_t count = sizeof(legacy);
        err = nvs_get_blob(nvs, "prefs", legacy, &count);
        if (err == ESP_OK) {
            if (!((count == 3 && legacy[0] == 1) ||
                  (count == 4 && legacy[0] == 2 && legacy[3] <= 1)) ||
                legacy[1] > 1 || legacy[2] > 1)
                err = ESP_ERR_INVALID_STATE;
            else
                next.settings = (display_settings_t){legacy[1] != 0, legacy[2] ? 180 : 0,
                                                     count == 3 || legacy[3] != 0};
        } else if (err == ESP_ERR_NVS_NOT_FOUND)
            err = ESP_OK;
    } else if (err == ESP_OK) {
        if (size != RECORD_SIZE || memcmp(data, "LDS1", 4) || data[4] > 1 || data[5] > 1 ||
            data[6] > 1 || data[7] > DISPLAY_RECEIPT_CONFLICT || data[16] > 1 || data[17] > 1 ||
            data[18] > 1 || data[19] > 1 || data[20] || data[21] || data[22] || data[23])
            err = ESP_ERR_INVALID_STATE;
        else {
            next.settings = (display_settings_t){data[4] != 0, data[5] ? 180 : 0, data[6] != 0};
            next.revision = get64(data + 8);
            next.receipt.state = (display_receipt_state_t)data[7];
            next.receipt.acknowledged = data[16] != 0;
            next.receipt.settings =
                (display_settings_t){data[17] != 0, data[18] ? 180 : 0, data[19] != 0};
            memcpy(next.receipt.command_id, data + 24, LINK_MAX_ID);
            next.receipt.ownership_generation = get64(data + 88);
            next.receipt.expected_revision = get64(data + 96);
            next.receipt.revision = get64(data + 104);
            display_receipt_t *r = &next.receipt;
            if (next.revision > DISPLAY_REVISION_MAX || !memchr(r->command_id, 0, LINK_MAX_ID) ||
                (r->state != DISPLAY_RECEIPT_NONE &&
                 (!link_id_valid(r->command_id) || !r->ownership_generation ||
                  r->ownership_generation > DISPLAY_REVISION_MAX || r->revision > next.revision ||
                  r->expected_revision >= DISPLAY_REVISION_MAX ||
                  (r->state == DISPLAY_RECEIPT_APPLIED ? r->revision != r->expected_revision + 1
                                                       : r->revision <= r->expected_revision))))
                err = ESP_ERR_INVALID_STATE;
        }
    }
    nvs_close(nvs);
    if (err == ESP_OK) {
        publish(next);
        s_ready = true;
    }
    return err;
}
void display_settings_load(display_settings_t *out)
{
    *out = display_settings_snapshot().settings;
}
bool display_settings_available(void)
{
    return s_ready;
}
bool display_settings_pending(void)
{
    display_receipt_t r = display_settings_snapshot().receipt;
    return r.state != DISPLAY_RECEIPT_NONE && !r.acknowledged;
}
esp_err_t display_settings_save(display_settings_t settings)
{
    if (!valid(settings))
        return ESP_ERR_INVALID_ARG;
    display_settings_snapshot_t next = display_settings_snapshot();
    if (!s_ready || next.revision == DISPLAY_REVISION_MAX)
        return ESP_ERR_INVALID_STATE;
    next.settings = settings;
    ++next.revision;
    return persist(next);
}
esp_err_t display_settings_apply(const char *id, uint64_t generation, uint64_t expected,
                                 display_settings_t settings)
{
    if (!link_id_valid(id) || !generation || generation > DISPLAY_REVISION_MAX ||
        expected >= DISPLAY_REVISION_MAX || !valid(settings))
        return ESP_ERR_INVALID_ARG;
    display_settings_snapshot_t next = display_settings_snapshot();
    if (!s_ready)
        return ESP_ERR_INVALID_STATE;
    if (next.receipt.state != DISPLAY_RECEIPT_NONE && !strcmp(id, next.receipt.command_id) &&
        generation == next.receipt.ownership_generation)
        return ESP_OK;
    if (display_settings_pending())
        return ESP_ERR_INVALID_STATE;
    if (expected > next.revision)
        return ESP_ERR_INVALID_ARG; // A future revision cannot have been observed.
    memset(&next.receipt, 0, sizeof(next.receipt));
    strcpy(next.receipt.command_id, id);
    next.receipt.ownership_generation = generation;
    next.receipt.expected_revision = expected;
    if (expected == next.revision) {
        next.settings = settings;
        ++next.revision;
        next.receipt.state = DISPLAY_RECEIPT_APPLIED;
    } else
        next.receipt.state = DISPLAY_RECEIPT_CONFLICT;
    next.receipt.revision = next.revision;
    next.receipt.settings = next.settings;
    return persist(next); // Settings, revision, and receipt commit atomically.
}
esp_err_t display_settings_ack(const display_receipt_t *r)
{
    display_settings_snapshot_t next = display_settings_snapshot();
    display_receipt_t *p = &next.receipt;
    if (!s_ready || p->state == DISPLAY_RECEIPT_NONE || strcmp(r->command_id, p->command_id) ||
        r->ownership_generation != p->ownership_generation ||
        r->expected_revision != p->expected_revision || r->revision != p->revision ||
        r->state != p->state)
        return ESP_ERR_INVALID_ARG;
    if (p->acknowledged)
        return ESP_OK;
    p->acknowledged = true;
    return persist(next);
}
