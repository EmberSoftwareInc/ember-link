#include "display_cloud.h"
#include "display_settings.h"
#include "cloud_protocol.h"
#include <string.h>

static const char *string(const cJSON *o, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsString(v) ? v->valuestring : NULL;
}
static const char *status(display_receipt_state_t s)
{
    return s == DISPLAY_RECEIPT_APPLIED ? "applied" : "conflict";
}
static void add_display(cJSON *body, display_settings_t s, uint64_t revision)
{
    cJSON *d = cJSON_AddObjectToObject(body, "display");
    cJSON_AddBoolToObject(d, "enabled", s.enabled);
    cJSON_AddNumberToObject(d, "rotation", s.rotation);
    cJSON_AddBoolToObject(d, "ledEnabled", s.led_enabled);
    cJSON_AddNumberToObject(d, "revision", (double)revision);
}
void display_settings_add_json(cJSON *body)
{
    display_settings_snapshot_t s = display_settings_snapshot();
    cJSON_AddNumberToObject(body, "settingsProtocolVersion", 1);
    cJSON_AddBoolToObject(body, "settingsWritable", display_settings_available());
    add_display(body, s.settings, s.revision);
}
void display_cloud_add_poll(cJSON *body)
{
    display_settings_snapshot_t s = display_settings_snapshot();
    cJSON_AddNumberToObject(body, "settingsProtocolVersion", 1);
    cJSON_AddBoolToObject(body, "settingsWritable", display_settings_available());
    add_display(body, s.settings, s.revision);
    display_receipt_t *r = &s.receipt;
    if (r->state == DISPLAY_RECEIPT_NONE)
        return;
    cJSON *o = cJSON_AddObjectToObject(body, "settingsReceipt");
    cJSON_AddStringToObject(o, "commandId", r->command_id);
    cJSON_AddStringToObject(o, "status", status(r->state));
    cJSON_AddNumberToObject(o, "ownershipGeneration", (double)r->ownership_generation);
    cJSON_AddNumberToObject(o, "expectedRevision", (double)r->expected_revision);
    cJSON_AddNumberToObject(o, "revision", (double)r->revision);
    add_display(o, r->settings, r->revision);
}
// Reject duplicate and unrecognized properties: this is a narrowly scoped settings command.
static bool keys(const cJSON *o, const char *const *allowed, size_t count)
{
    if (!cJSON_IsObject(o) || cJSON_GetArraySize(o) != (int)count)
        return false;
    for (size_t i = 0; i < count; i++) {
        unsigned matches = 0;
        const cJSON *v;
        cJSON_ArrayForEach(v, o)
        {
            if (v->string && !strcmp(v->string, allowed[i]))
                ++matches;
        }
        if (matches != 1)
            return false;
    }
    return true;
}
esp_err_t display_cloud_run(const cJSON *command, uint64_t generation, time_t now)
{
    static const char *const fields[] = {"commandId", "ownershipGeneration", "expectedRevision",
                                         "expiresAt", "display"};
    static const char *const display_fields[] = {"enabled", "rotation", "ledEnabled"};
    const cJSON *d = cJSON_GetObjectItemCaseSensitive(command, "display");
    const cJSON *enabled = cJSON_GetObjectItemCaseSensitive(d, "enabled"),
                *led = cJSON_GetObjectItemCaseSensitive(d, "ledEnabled");
    const char *id = string(command, "commandId");
    uint64_t owner, expected, expiry, rotation;
    if (!keys(command, fields, 5) || !keys(d, display_fields, 3) || !link_id_valid(id) ||
        !link_json_u64(command, "ownershipGeneration", 1, DISPLAY_REVISION_MAX, &owner) ||
        owner != generation ||
        !link_json_u64(command, "expectedRevision", 0, DISPLAY_REVISION_MAX - 1, &expected) ||
        !link_json_u64(command, "expiresAt", 1, DISPLAY_REVISION_MAX, &expiry) ||
        now < 1735689600 || expiry <= (uint64_t)now || expiry > (uint64_t)now + 3600 ||
        !cJSON_IsBool(enabled) || !cJSON_IsBool(led) ||
        !link_json_u64(d, "rotation", 0, 180, &rotation) || (rotation != 0 && rotation != 180))
        return ESP_ERR_INVALID_ARG;
    return display_settings_apply(
        id, owner, expected,
        (display_settings_t){cJSON_IsTrue(enabled), (uint16_t)rotation, cJSON_IsTrue(led)});
}
esp_err_t display_cloud_ack(const cJSON *ack)
{
    const char *id = string(ack, "commandId"), *state = string(ack, "status");
    display_receipt_t r = {0};
    if (!link_id_valid(id) || !state || (strcmp(state, "applied") && strcmp(state, "conflict")) ||
        !link_json_u64(ack, "ownershipGeneration", 1, DISPLAY_REVISION_MAX,
                       &r.ownership_generation) ||
        !link_json_u64(ack, "expectedRevision", 0, DISPLAY_REVISION_MAX - 1,
                       &r.expected_revision) ||
        !link_json_u64(ack, "revision", 0, DISPLAY_REVISION_MAX, &r.revision))
        return ESP_ERR_INVALID_ARG;
    strcpy(r.command_id, id);
    r.state = !strcmp(state, "applied") ? DISPLAY_RECEIPT_APPLIED : DISPLAY_RECEIPT_CONFLICT;
    return display_settings_ack(&r);
}
