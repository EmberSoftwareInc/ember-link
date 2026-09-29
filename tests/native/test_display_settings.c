#include "display_settings.h"
#include "display_cloud.h"
#include "settings_fixture.h"
#include <stdio.h>
static bool fail_open;
esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *out)
{
    assert(!strcmp(name, "linkdisplay"));
    (void)mode;
    *out = 1;
    return fail_open ? ESP_FAIL : ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *out, size_t *size)
{
    (void)h;
    return settings_read(key, out, size);
}
esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *data, size_t size)
{
    (void)h;
    return settings_write(key, data, size);
}
esp_err_t nvs_commit(nvs_handle_t h)
{
    (void)h;
    return settings_commit();
}
void nvs_close(nvs_handle_t h)
{
    (void)h;
}
static void reset(void)
{
    settings_size = settings_legacy_size = 0;
    settings_fail_write = settings_fail_commit = settings_commit_ambiguous = fail_open = false;
    assert(display_settings_init() == ESP_OK);
}
static cJSON *command(const char *id, uint64_t revision)
{
    char text[512];
    snprintf(
        text, sizeof(text),
        "{\"commandId\":\"%s\",\"ownershipGeneration\":7,\"expectedRevision\":%llu,\"expiresAt\":"
        "1800000060,\"display\":{\"enabled\":false,\"rotation\":180,\"ledEnabled\":false}}",
        id, (unsigned long long)revision);
    cJSON *o = cJSON_Parse(text);
    assert(o);
    return o;
}
static cJSON *ack(void)
{
    cJSON *o = cJSON_CreateObject();
    display_cloud_add_poll(o);
    cJSON *r = cJSON_DetachItemFromObject(o, "settingsReceipt");
    cJSON_Delete(o);
    assert(r);
    return r;
}
int main(void)
{
    reset();
    display_settings_t s;
    display_settings_load(&s);
    assert(s.enabled && s.rotation == 0 && s.led_enabled);
    // Upgrade imports both legacy encodings without changing the rollback record.
    settings_legacy_size = 3;
    settings_legacy[0] = 1;
    settings_legacy[1] = 0;
    settings_legacy[2] = 1;
    assert(display_settings_init() == ESP_OK);
    display_settings_load(&s);
    assert(!s.enabled && s.rotation == 180 && s.led_enabled);
    settings_legacy_size = 4;
    settings_legacy[0] = 2;
    settings_legacy[3] = 0;
    assert(display_settings_init() == ESP_OK);
    display_settings_load(&s);
    assert(!s.enabled && !s.led_enabled);
    assert(display_settings_save(s) == ESP_OK);
    assert(settings_legacy[0] == 2 && settings_legacy_size == 4);
    assert(display_settings_init() == ESP_OK && display_settings_snapshot().revision == 1);
    // Even a same-value local save invalidates a queued request.
    cJSON *o = command("first", 1);
    assert(display_settings_save(s) == ESP_OK);
    assert(display_cloud_run(o, 7, 1800000000) == ESP_OK);
    display_settings_snapshot_t snap = display_settings_snapshot();
    assert(snap.revision == 2 && snap.receipt.state == DISPLAY_RECEIPT_CONFLICT &&
           display_settings_pending());
    assert(display_settings_init() == ESP_OK && display_settings_pending());
    cJSON *a = ack();
    cJSON_ReplaceItemInObject(a, "ownershipGeneration", cJSON_CreateNumber(8));
    assert(display_cloud_ack(a) == ESP_ERR_INVALID_ARG && display_settings_pending());
    cJSON_ReplaceItemInObject(a, "ownershipGeneration", cJSON_CreateNumber(7));
    assert(display_cloud_ack(a) == ESP_OK && !display_settings_pending());
    cJSON_Delete(a);
    cJSON_Delete(o);
    o = command("second", 2);
    assert(display_cloud_run(o, 7, 1800000000) == ESP_OK);
    assert(display_settings_init() == ESP_OK);
    snap = display_settings_snapshot();
    assert(snap.revision == 3 && snap.receipt.state == DISPLAY_RECEIPT_APPLIED &&
           !snap.settings.enabled && !snap.settings.led_enabled && snap.settings.rotation == 180);
    unsigned writes = settings_writes;
    assert(display_cloud_run(o, 7, 1800000000) == ESP_OK &&
           settings_writes == writes); // Lost response / reboot replay.
    cJSON *another = command("third", 3);
    assert(display_cloud_run(another, 7, 1800000000) == ESP_ERR_INVALID_STATE);
    // A USB update remains possible while receipt awaits acknowledgement; it must not rewrite
    // receipt history.
    assert(display_settings_save(display_settings_defaults()) == ESP_OK);
    a = ack();
    assert(cJSON_GetObjectItem(a, "revision")->valuedouble == 3);
    assert(!cJSON_IsTrue(cJSON_GetObjectItem(cJSON_GetObjectItem(a, "display"), "enabled")));
    assert(display_cloud_ack(a) == ESP_OK);
    cJSON_Delete(a);
    assert(display_cloud_run(o, 7, 1800000000) == ESP_OK &&
           display_settings_snapshot().revision == 4);
    assert(display_settings_snapshot().settings.enabled); // Replay cannot undo USB choice.
    assert(display_cloud_run(another, 7, 1800000000) == ESP_OK &&
           display_settings_snapshot().receipt.state == DISPLAY_RECEIPT_CONFLICT);
    cJSON_Delete(o);
    cJSON_Delete(another);
    // Strict wire validation: ownership, lifetime, fields, types and safe integer bounds.
    reset();
    o = command("valid", 0);
    writes = settings_writes;
    assert(display_cloud_run(o, 8, 1800000000) == ESP_ERR_INVALID_ARG);
    assert(display_cloud_run(o, 7, 1800000060) == ESP_ERR_INVALID_ARG);
    assert(display_cloud_run(o, 7, 1700000000) == ESP_ERR_INVALID_ARG);
    assert(display_cloud_run(o, 7, 1799990000) == ESP_ERR_INVALID_ARG);
    cJSON_AddNumberToObject(o, "expectedRevision", 0);
    assert(display_cloud_run(o, 7, 1800000000) == ESP_ERR_INVALID_ARG);
    cJSON_Delete(o);
    o = command("valid", 0);
    cJSON_AddStringToObject(o, "wifiPassword", "forbidden");
    assert(display_cloud_run(o, 7, 1800000000) == ESP_ERR_INVALID_ARG);
    cJSON_DeleteItemFromObject(o, "wifiPassword");
    cJSON *d = cJSON_GetObjectItem(o, "display");
    cJSON_ReplaceItemInObject(d, "enabled", cJSON_CreateNumber(0));
    assert(display_cloud_run(o, 7, 1800000000) == ESP_ERR_INVALID_ARG);
    cJSON_ReplaceItemInObject(d, "enabled", cJSON_CreateBool(false));
    cJSON_ReplaceItemInObject(d, "rotation", cJSON_CreateNumber(90));
    assert(display_cloud_run(o, 7, 1800000000) == ESP_ERR_INVALID_ARG);
    cJSON_ReplaceItemInObject(d, "rotation", cJSON_CreateNumber(180));
    cJSON_DeleteItemFromObject(d, "ledEnabled");
    assert(display_cloud_run(o, 7, 1800000000) == ESP_ERR_INVALID_ARG);
    cJSON_AddBoolToObject(d, "ledEnabled", false);
    const double invalid_revisions[] = {-1, 0.5, 9007199254740991.0, 9007199254740992.0, 1};
    for (unsigned i = 0; i < sizeof(invalid_revisions) / sizeof(invalid_revisions[0]); i++) {
        cJSON_ReplaceItemInObject(o, "expectedRevision", cJSON_CreateNumber(invalid_revisions[i]));
        assert(display_cloud_run(o, 7, 1800000000) == ESP_ERR_INVALID_ARG);
    }
    assert(settings_writes == writes && display_settings_snapshot().revision == 0);
    cJSON_ReplaceItemInObject(o, "expectedRevision", cJSON_CreateNumber(0));
    // No success or RAM change when NVS fails. Ambiguous commits require reboot/readback.
    fail_open = true;
    assert(display_cloud_run(o, 7, 1800000000) == ESP_FAIL);
    fail_open = false;
    assert(display_settings_init() == ESP_OK);
    settings_fail_write = true;
    assert(display_cloud_run(o, 7, 1800000000) == ESP_FAIL);
    settings_fail_write = false;
    assert(display_settings_snapshot().revision == 0 && !display_settings_pending());
    assert(display_settings_init() == ESP_OK);
    settings_fail_commit = true;
    assert(display_cloud_run(o, 7, 1800000000) == ESP_FAIL);
    settings_fail_commit = false;
    assert(display_settings_init() == ESP_OK && display_settings_snapshot().revision == 0);
    settings_fail_commit = settings_commit_ambiguous = true;
    assert(display_cloud_run(o, 7, 1800000000) == ESP_FAIL);
    assert(display_settings_save(display_settings_defaults()) == ESP_ERR_INVALID_STATE);
    settings_fail_commit = settings_commit_ambiguous = false;
    assert(display_settings_init() == ESP_OK && display_settings_snapshot().revision == 1 &&
           display_settings_pending());
    writes = settings_writes;
    assert(display_cloud_run(o, 7, 1800000000) == ESP_OK && settings_writes == writes);
    a = ack();
    settings_fail_commit = true;
    assert(display_cloud_ack(a) == ESP_FAIL);
    settings_fail_commit = false;
    assert(display_settings_init() == ESP_OK && display_settings_pending());
    assert(display_cloud_ack(a) == ESP_OK);
    cJSON_Delete(a);
    cJSON_Delete(o);
    // Unknown/truncated storage is not silently interpreted as revision zero.
    settings_disk[0] = 'X';
    assert(display_settings_init() == ESP_ERR_INVALID_STATE);
    assert(display_settings_save(display_settings_defaults()) == ESP_ERR_INVALID_STATE);
    settings_disk[0] = 'L';
    settings_size = 20;
    assert(display_settings_init() == ESP_ERR_INVALID_STATE);
    reset();
    settings_legacy_size = 4;
    settings_legacy[0] = 9;
    assert(display_settings_init() == ESP_ERR_INVALID_STATE);
    reset();
    assert(display_settings_save((display_settings_t){true, 90, true}) == ESP_ERR_INVALID_ARG);
    // Revision exhaustion cannot wrap and make stale commands eligible.
    assert(display_settings_save(display_settings_defaults()) == ESP_OK);
    for (unsigned i = 0; i < 8; i++)
        settings_disk[8 + i] = (uint8_t)(DISPLAY_REVISION_MAX >> (8 * i));
    assert(display_settings_init() == ESP_OK);
    assert(display_settings_save(display_settings_defaults()) == ESP_ERR_INVALID_STATE);
    puts("Display settings tests passed (USB/cloud revisions, atomic receipts, replay, conflicts, "
         "migration, NVS faults, strict wire contract)");
}
