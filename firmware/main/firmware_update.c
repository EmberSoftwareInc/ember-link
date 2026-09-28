#include "firmware_update.h"
#include "cloud_protocol.h"
#include "cloud_settings.h"
#include "ota.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_secure_boot.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_partition.h"
#include "nvs.h"
#include "psa/crypto.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

typedef struct {
    uint32_t schema;
    bool acknowledged;
    char update_id[64], attempt_id[64], release_id[64], version[32], sha256[65];
    char previous_slot[17], state[24], error[48];
    uint64_t generation, size;
} update_record_t;
static update_record_t s_update;
static const char *str(const cJSON *o, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsString(v) ? v->valuestring : NULL;
}
static bool same(const char *a, const char *b) { return a && b && !strcmp(a, b); }
static esp_err_t persist(const update_record_t *record)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("link_cloud", NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(nvs, "update_v1", record, sizeof(*record));
    if (err == ESP_OK) err = nvs_commit(nvs);
    nvs_close(nvs);
    return err;
}
static void settle_boot(void)
{
    update_record_t next = s_update;
    ota_status_t status;
    ota_get_status(&status);
    if (same(next.state, "downloading")) {
        strcpy(next.state, "failed"); strcpy(next.error, "interrupted");
    } else if (same(next.state, "rebooting")) {
        if (status.pending_verify) return;
        if (same(status.version, next.version) && !same(status.slot, next.previous_slot)) {
            strcpy(next.state, "installed");
        } else {
            strcpy(next.state, "rolled_back"); strcpy(next.error, "boot_not_confirmed");
        }
    } else return;
    if (persist(&next) == ESP_OK) s_update = next;
}
esp_err_t firmware_update_init(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("link_cloud", NVS_READONLY, &nvs);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;
    size_t size = sizeof(s_update);
    err = nvs_get_blob(nvs, "update_v1", &s_update, &size);
    nvs_close(nvs);
    if (err == ESP_ERR_NVS_NOT_FOUND) { memset(&s_update, 0, sizeof(s_update)); return ESP_OK; }
    if (err != ESP_OK) return err;
    if (size != sizeof(s_update) || s_update.schema != 1) return ESP_ERR_INVALID_STATE;
#define TERMINATED(field) memchr(s_update.field, 0, sizeof(s_update.field))
    if (!TERMINATED(update_id) || !TERMINATED(attempt_id) || !TERMINATED(release_id) ||
        !TERMINATED(version) || !TERMINATED(sha256) || !TERMINATED(previous_slot) ||
        !TERMINATED(state) || !TERMINATED(error) || !link_id_valid(s_update.update_id) ||
        !link_id_valid(s_update.attempt_id)) return ESP_ERR_INVALID_STATE;
#undef TERMINATED
    // Only init interprets an interrupted download. Polling must not do so.
    if (same(s_update.state, "downloading")) settle_boot();
    return ESP_OK;
}
bool firmware_update_pending(void) { return s_update.update_id[0] && !s_update.acknowledged; }
static cJSON *trusted_keys(void)
{
    cJSON *keys = cJSON_CreateArray();
    esp_image_sig_public_key_digests_t digests;
    bool loaded = esp_secure_boot_get_signature_blocks_for_running_app(true, &digests) == ESP_OK;
    if (loaded) {
        for (unsigned i = 0; i < digests.num_digests; ++i) {
            char hex[65];
            for (unsigned j = 0; j < 32; ++j) snprintf(hex + j * 2, 3, "%02x", digests.key_digests[i][j]);
            cJSON_AddItemToArray(keys, cJSON_CreateString(hex));
        }
    }
    return keys;
}
void firmware_update_add_capabilities(cJSON *body)
{
    ota_status_t status;
    ota_get_status(&status);
    cJSON *caps = cJSON_AddObjectToObject(body, "capabilities");
    cJSON_AddNumberToObject(caps, "firmwareUpdate", 1);
    cJSON_AddStringToObject(caps, "boardId", LINK_BOARD_ID);
    cJSON_AddStringToObject(caps, "layoutId", LINK_LAYOUT_ID);
    cJSON_AddNumberToObject(caps, "settingsSchema", LINK_SETTINGS_SCHEMA);
    cJSON_AddNumberToObject(caps, "maxImageSize", status.max_image_size);
    cJSON_AddItemToObject(caps, "trustedKeyIds", trusted_keys());
    const esp_partition_t *reserved = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, "link_future");
    cJSON_AddNumberToObject(caps, "reservedCredentialBytes", reserved ? reserved->size : 0);
 }
void firmware_update_add_poll(cJSON *body)
{
    if (same(s_update.state, "rebooting")) settle_boot();
    firmware_update_add_capabilities(body);
    ota_status_t status;
    ota_get_status(&status);
    cJSON *diag = cJSON_AddObjectToObject(body, "diagnostics");
    cJSON_AddNumberToObject(diag, "freeHeap", esp_get_free_heap_size());
    cJSON_AddNumberToObject(diag, "minimumFreeHeap", esp_get_minimum_free_heap_size());
    cJSON_AddNumberToObject(diag, "resetReason", esp_reset_reason());
    cJSON_AddBoolToObject(diag, "pendingVerify", status.pending_verify);
    if (!s_update.update_id[0]) return;
    cJSON *r = cJSON_AddObjectToObject(body, "firmwareReceipt");
    cJSON_AddStringToObject(r, "updateId", s_update.update_id);
    cJSON_AddStringToObject(r, "attemptId", s_update.attempt_id);
    cJSON_AddStringToObject(r, "releaseId", s_update.release_id);
    cJSON_AddStringToObject(r, "targetVersion", s_update.version);
    cJSON_AddStringToObject(r, "sha256", s_update.sha256);
    cJSON_AddNumberToObject(r, "size", (double)s_update.size);
    cJSON_AddNumberToObject(r, "ownershipGeneration", (double)s_update.generation);
    cJSON_AddStringToObject(r, "state", s_update.state);
    cJSON_AddStringToObject(r, "errorCode", s_update.error);
}
void firmware_update_ack(const cJSON *ack)
{
    if (!firmware_update_pending() || !same(str(ack, "updateId"), s_update.update_id) ||
        !same(str(ack, "attemptId"), s_update.attempt_id) || !same(str(ack, "state"), s_update.state)) return;
    if (!same(s_update.state, "installed") && !same(s_update.state, "failed") && !same(s_update.state, "rolled_back")) return;
    update_record_t next = s_update;
    next.acknowledged = true;
    if (persist(&next) == ESP_OK) s_update = next;
}
static esp_err_t stream_image(const char *url, time_t expiry, uint64_t size, const char *expected_hash)
{
    esp_http_client_config_t cfg = {.url = url, .timeout_ms = 15000, .crt_bundle_attach = esp_crt_bundle_attach,
        .disable_auto_redirect = true, .buffer_size = 4096};
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return ESP_ERR_NO_MEM;
    psa_hash_operation_t hash = PSA_HASH_OPERATION_INIT;
    esp_err_t err = ESP_FAIL;
    if (psa_hash_setup(&hash, PSA_ALG_SHA_256) != PSA_SUCCESS) goto done;
    if (esp_http_client_open(client, 0) != ESP_OK) goto done;
    if (esp_http_client_fetch_headers(client) != (int64_t)size || esp_http_client_get_status_code(client) != 200) goto done;
    uint64_t received = 0;
    int64_t deadline = esp_timer_get_time() + 300LL * 1000000;
    unsigned char buffer[4096];
    while (received < size) {
        if (time(NULL) >= expiry || esp_timer_get_time() >= deadline) { err = ESP_ERR_TIMEOUT; goto done; }
        int want = size - received > sizeof(buffer) ? sizeof(buffer) : (int)(size - received);
        int n = esp_http_client_read(client, (char *)buffer, want);
        if (n <= 0) goto done;
        if (psa_hash_update(&hash, buffer, n) != PSA_SUCCESS) goto done;
        err = ota_write(buffer, n);
        if (err != ESP_OK) goto done;
        err = ESP_FAIL;
        received += n;
    }
    if (!esp_http_client_is_complete_data_received(client)) goto done;
    unsigned char digest[32]; size_t length = 0; char hex[65];
    if (psa_hash_finish(&hash, digest, sizeof(digest), &length) != PSA_SUCCESS || length != 32) goto done;
    for (unsigned i = 0; i < 32; ++i) snprintf(hex + 2*i, 3, "%02x", digest[i]);
    err = same(hex, expected_hash) ? ESP_OK : ESP_ERR_INVALID_CRC;
done:
    psa_hash_abort(&hash);
    esp_http_client_cleanup(client);
    return err;
}
void firmware_update_run(const cJSON *job, uint64_t generation, const char *host)
{
    if (firmware_update_pending()) return;
    const char *id = str(job, "updateId"), *attempt = str(job, "attemptId"), *release = str(job, "releaseId");
    const char *version = str(job, "targetVersion"), *sha = str(job, "sha256"), *url = str(job, "downloadUrl");
    uint64_t size, expiry, owner;
    ota_status_t status; ota_get_status(&status);
    if (!link_id_valid(id) || same(id, s_update.update_id) || !link_id_valid(attempt) || !link_id_valid(release) ||
        !version || !version[0] || strlen(version) >= sizeof(s_update.version) || same(version, status.version) ||
        !link_sha256_valid(sha) || !same(str(job, "boardId"), LINK_BOARD_ID) || !same(str(job, "layoutId"), LINK_LAYOUT_ID) ||
        !link_json_u64(job, "size", 1, status.max_image_size, &size) ||
        !link_json_u64(job, "ownershipGeneration", generation, generation, &owner) ||
        !link_json_u64(job, "expiresAt", (uint64_t)time(NULL) + 1, (uint64_t)time(NULL) + 3600, &expiry) ||
        !link_https_url_valid(url, host) || status.pending_verify) return;
    cJSON *keys = trusted_keys(); const cJSON *key; bool trusted = false;
    cJSON_ArrayForEach(key, keys) if (same(cJSON_GetStringValue(key), str(job, "signingKeyId"))) trusted = true;
    cJSON_Delete(keys);
    if (!trusted || ota_begin(size) != ESP_OK) return;
    update_record_t next = {.schema = 1, .size = size, .generation = generation};
    strcpy(next.update_id, id); strcpy(next.attempt_id, attempt); strcpy(next.release_id, release);
    strcpy(next.version, version); strcpy(next.sha256, sha); strcpy(next.previous_slot, status.slot);
    strcpy(next.state, "downloading");
    if (persist(&next) != ESP_OK) { ota_abort(); return; }
    s_update = next;
    esp_err_t err = ota_expect_version(version);
    if (err == ESP_OK) err = stream_image(url, expiry, size, sha);
    if (err == ESP_OK) err = ota_prepare(); // verifies RSA signature, does not select boot slot
    if (err == ESP_OK) {
        strcpy(next.state, "rebooting");
        err = persist(&next);
        if (err == ESP_OK) {
            s_update = next;
            err = ota_activate();
            if (err == ESP_OK) { esp_restart(); return; }
        }
    }
    ota_abort();
    strcpy(next.state, "failed");
    strcpy(next.error, err == ESP_ERR_INVALID_CRC ? "checksum_mismatch" : "update_failed");
    if (persist(&next) == ESP_OK) s_update = next;
    // Failed persistence leaves the durable interrupted-download record for recovery.
}
