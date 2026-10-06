#include "display.h"
#include "display_cloud.h"
// Ember Link cloud transfer v1. All connections originate at the dongle.
// No backend endpoint or credential is shipped in the firmware image.
#include "cloud.h"
#include "cloud_settings.h"
#include "firmware_update.h"
#include "cloud_protocol.h"
#include "app_version.h"
#include "link_files.h"
#include "link_protocol.h"
#include "operation.h"
#include "storage.h"
#include "wifi_mgr.h"
#include "led.h"

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_netif_sntp.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CLOUD_SCHEMA 1
#define RESPONSE_MAX 8192
#define API_TIMEOUT_MS 15000
#define DOWNLOAD_TIMEOUT_MS 15000
#define TRANSFER_LIMIT_US (300LL * 1000000)

typedef struct {
    uint32_t schema;
    bool enabled;
    char api_base[241];
    char download_host[254];
    char device_id[LINK_MAX_ID];
    char token[65];
} cloud_config_t;

// A separate journal leaves the deployed config schema and existing devices intact.
// Retain the candidate credential across lost responses, expired tickets and reboot.
typedef struct {
    uint32_t schema;
    bool active;
    cloud_config_t candidate;
    char session_id[LINK_MAX_ID];
    char ticket[65];
} cloud_enrollment_t;

static cloud_enrollment_t s_enrollment;
static bool s_enrollment_fault;
static const char *s_enrollment_state = "idle";
static int64_t s_enrollment_deadline;

static cloud_config_t s_config;
static link_receipt_t s_receipt;
static SemaphoreHandle_t s_session;
static TaskHandle_t s_worker;
static const char *s_state = "disabled";
static bool s_sntp_started;
static bool s_started;
// Numeric transport diagnostics only: never retain URLs, headers or payloads.
typedef struct {
    const char *stage;
    int http_status, error, socket_errno, transport_error, tls_error, tls_flags;
} cloud_network_status_t;
static cloud_network_status_t s_network;
// Ephemeral USB setup proof. The long-lived device token never leaves NVS.
static struct {
    char session_id[LINK_MAX_ID];
    char secret[65];
    int64_t expires_us;
    const char *state;
} s_setup;


// Only this explicitly redacted snapshot crosses the session-lock boundary.
// Pointers refer to string literals; the short critical section only copies data.
typedef struct {
    int64_t captured_us;
    bool configured, enabled, enrollment_pending;
    const char *state, *enrollment_state, *setup_state;
    char device_id[LINK_MAX_ID], enrollment_device_id[LINK_MAX_ID];
    char enrollment_session_id[LINK_MAX_ID], setup_session_id[LINK_MAX_ID];
    cloud_network_status_t network;
} cloud_status_snapshot_t;
static portMUX_TYPE s_status_lock = portMUX_INITIALIZER_UNLOCKED;
static cloud_status_snapshot_t s_status_snapshot;

static const char *string(const cJSON *o, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsString(v) ? v->valuestring : NULL;
}

static esp_err_t save(const char *key, const void *data, size_t size)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("link_cloud", NVS_READWRITE, &nvs);
    if (err != ESP_OK)
        return err;
    err = nvs_set_blob(nvs, key, data, size);
    if (err == ESP_OK)
        err = nvs_commit(nvs);
    nvs_close(nvs);
    return err;
}

static esp_err_t read_blob(const char *key, void *data, size_t size)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("link_cloud", NVS_READONLY, &nvs);
    if (err == ESP_ERR_NVS_NOT_FOUND)
        return ESP_ERR_NOT_FOUND;
    if (err != ESP_OK)
        return err;
    size_t actual = size;
    err = nvs_get_blob(nvs, key, data, &actual);
    nvs_close(nvs);
    if (err == ESP_ERR_NVS_NOT_FOUND)
        return ESP_ERR_NOT_FOUND;
    return err == ESP_OK && actual != size ? ESP_ERR_INVALID_SIZE : err;
}

static bool config_valid(const cloud_config_t *c)
{
    return c->schema == CLOUD_SCHEMA && memchr(c->api_base, 0, sizeof(c->api_base)) &&
           memchr(c->download_host, 0, sizeof(c->download_host)) &&
           memchr(c->device_id, 0, sizeof(c->device_id)) && memchr(c->token, 0, sizeof(c->token)) &&
           link_api_base_valid(c->api_base) && link_host_valid(c->download_host) &&
           link_id_valid(c->device_id) && link_token_valid(c->token);
}

static bool enrollment_pending(void)
{
    return s_enrollment.session_id[0] != 0;
}

static esp_err_t enrollment_save(const cloud_enrollment_t *next)
{
    esp_err_t err = save("enrollment", next, sizeof(*next));
    if (err == ESP_OK) s_enrollment = *next;
    else {
        // A failed commit may have reached flash. Reload on boot before using
        // or replacing any credential; never send an unjournaled credential.
        s_enrollment_fault = true;
        s_enrollment_state = "storage_error";
    }
    return err;
}

static bool enrollment_valid(const cloud_enrollment_t *e)
{
    if (e->schema != 1 || !memchr(e->session_id, 0, sizeof(e->session_id)) ||
        !memchr(e->ticket, 0, sizeof(e->ticket))) return false;
    if (!e->session_id[0]) return !e->active && !e->ticket[0];
    cloud_config_t c = e->candidate;
    // A pending request may not have generated its credential yet (no Wi-Fi).
    if (!c.token[0]) memset(c.token, '0', 64);
    return link_id_valid(e->session_id) && link_token_valid(e->ticket) && config_valid(&c);
}

static bool enrollment_matches_config(void)
{
    const cloud_config_t *c = &s_enrollment.candidate;
    return config_valid(&s_config) && !strcmp(c->api_base, s_config.api_base) &&
           !strcmp(c->download_host, s_config.download_host) &&
           !strcmp(c->device_id, s_config.device_id) && !strcmp(c->token, s_config.token);
}

static bool unsettled(void)
{
    return (s_receipt.job_id[0] && !s_receipt.acknowledged) || firmware_update_pending() ||
           display_settings_pending() || !display_settings_available();
}

// Caller owns s_session, or is initializing before any worker starts.
static void publish_status(void)
{
    cloud_status_snapshot_t next = {
        .captured_us = esp_timer_get_time(),
        .configured = config_valid(&s_config),
        .enabled = s_config.enabled,
        .enrollment_pending = enrollment_pending(),
        .state = s_state,
        .enrollment_state = s_enrollment_state,
        .setup_state = s_setup.state ? s_setup.state : "idle",
        .network = s_network,
    };
    if (next.configured) strcpy(next.device_id, s_config.device_id);
    if (next.enrollment_pending) {
        strcpy(next.enrollment_device_id, s_enrollment.candidate.device_id);
        strcpy(next.enrollment_session_id, s_enrollment.session_id);
    }
    strcpy(next.setup_session_id, s_setup.session_id);
    portENTER_CRITICAL(&s_status_lock);
    s_status_snapshot = next;
    portEXIT_CRITICAL(&s_status_lock);
}

static cloud_status_snapshot_t status_snapshot(void)
{
    portENTER_CRITICAL(&s_status_lock);
    cloud_status_snapshot_t copy = s_status_snapshot;
    portEXIT_CRITICAL(&s_status_lock);
    return copy;
}

static cJSON *receipt_json(void)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "jobId", s_receipt.job_id);
    cJSON_AddStringToObject(o, "attemptId", s_receipt.attempt_id);
    cJSON_AddStringToObject(o, "state", s_receipt.state);
    cJSON_AddStringToObject(o, "filename", s_receipt.filename);
    cJSON_AddStringToObject(o, "sha256", s_receipt.sha256);
    cJSON_AddNumberToObject(o, "size", (double)s_receipt.size);
    cJSON_AddNumberToObject(o, "bytesReceived", (double)s_receipt.bytes);
    cJSON_AddNumberToObject(o, "ownershipGeneration", (double)s_receipt.ownership_generation);
    if (s_receipt.error[0])
        cJSON_AddStringToObject(o, "errorCode", s_receipt.error);
    return o;
}

// Explicitly bounds the response, rejects redirects, and checks TLS roots and
// hostname. Never log HTTP bodies, Authorization, or presigned download URLs.
static cJSON *https_post(const char *base, const char *token, const char *route, cJSON *body, int *status)
{
    *status = 0;
    memset(&s_network, 0, sizeof(s_network));
    s_network.stage = "init";
    publish_status();
    char url[384], authorization[80];
    snprintf(url, sizeof(url), "%s%s", base, route);
    snprintf(authorization, sizeof(authorization), "Bearer %s", token ? token : "");
    char *payload = cJSON_PrintUnformatted(body);
    char *response = calloc(1, RESPONSE_MAX + 1);
    if (!payload || !response) {
        free(payload);
        free(response);
        return NULL;
    }
    esp_http_client_config_t cfg = {.url = url,
                                    .method = HTTP_METHOD_POST,
                                    .timeout_ms = API_TIMEOUT_MS,
                                    .crt_bundle_attach = esp_crt_bundle_attach,
                                    .disable_auto_redirect = true,
                                    .buffer_size = 2048,
                                    .buffer_size_tx = 2048};
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    cJSON *result = NULL;
    if (!client)
        goto cleanup;
    if (token) esp_http_client_set_header(client, "Authorization", authorization);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    size_t len = strlen(payload), sent = 0;
    s_network.stage = "connect";
    publish_status();
    s_network.error = esp_http_client_open(client, (int)len);
    if (s_network.error != ESP_OK)
        goto cleanup;
    s_network.stage = "write";
    publish_status();
    while (sent < len) {
        int n = esp_http_client_write(client, payload + sent, (int)(len - sent));
        if (n <= 0)
            goto cleanup;
        sent += (size_t)n;
    }
    s_network.stage = "headers";
    publish_status();
    int64_t expected = esp_http_client_fetch_headers(client);
    *status = esp_http_client_get_status_code(client);
    s_network.http_status = *status;
    if (expected < 0 || expected > RESPONSE_MAX || *status != 200)
        goto cleanup;
    s_network.stage = "read";
    publish_status();
    size_t used = 0;
    while (used < RESPONSE_MAX) {
        int n = esp_http_client_read(client, response + used, (int)(RESPONSE_MAX - used));
        if (n < 0)
            goto cleanup;
        if (!n)
            break;
        used += (size_t)n;
    }
    if (!esp_http_client_is_complete_data_received(client))
        goto cleanup;
    s_network.stage = "parse";
    publish_status();
    result = cJSON_ParseWithLengthOpts(response, used + 1, NULL, true);
    if (!cJSON_IsObject(result)) {
        cJSON_Delete(result);
        result = NULL;
    }
cleanup:
    if (result) s_network.stage = "complete";
    if (client) {
        if (!result) {
            s_network.socket_errno = esp_http_client_get_errno(client);
            s_network.transport_error = esp_http_client_get_and_clear_last_tls_error(client, &s_network.tls_error, &s_network.tls_flags);
        }
        esp_http_client_cleanup(client);
    }
    memset(authorization, 0, sizeof(authorization));
    free(payload);
    free(response);
    publish_status();
    return result;
}

static cJSON *api_post(const char *route, cJSON *body, int *status)
{
    return https_post(s_config.api_base, s_config.token, route, body, status);
}

static esp_err_t enrollment_pause(const char *state)
{
    cloud_enrollment_t next = s_enrollment;
    next.active = false;
    esp_err_t err = enrollment_save(&next);
    if (err == ESP_OK) s_enrollment_state = state;
    return err;
}

// Called only with a station connection and a sane clock, under the session lock.
static unsigned enroll_cloud(void)
{
    if (s_enrollment_fault || !enrollment_pending() || !s_enrollment.active) return 60;
    if (esp_timer_get_time() >= s_enrollment_deadline) {
        (void)enrollment_pause("retry_required");
        return 60;
    }
    if (unsettled() || !operation_begin()) return 5;
    unsigned delay = 0;
    if (!s_enrollment.candidate.token[0]) {
        cloud_enrollment_t next = s_enrollment;
        uint8_t random[32];
        // Wi-Fi is active here, supplying the ESP32 hardware RNG's entropy.
        esp_fill_random(random, sizeof(random));
        for (size_t i = 0; i < sizeof(random); ++i)
            snprintf(next.candidate.token + 2 * i, 3, "%02x", random[i]);
        memset(random, 0, sizeof(random));
        esp_err_t err = enrollment_save(&next);
        memset(&next, 0, sizeof(next));
        if (err != ESP_OK) goto done;
    }
    cJSON *body = cJSON_CreateObject();
    // Do not transmit a partial request on allocation failure.
    if (!body || !cJSON_AddNumberToObject(body, "protocolVersion", 1) ||
        !cJSON_AddStringToObject(body, "sessionId", s_enrollment.session_id) ||
        !cJSON_AddStringToObject(body, "ticket", s_enrollment.ticket) ||
        !cJSON_AddStringToObject(body, "deviceId", s_enrollment.candidate.device_id) ||
        !cJSON_AddStringToObject(body, "deviceToken", s_enrollment.candidate.token) ||
        !cJSON_AddStringToObject(body, "firmwareVersion", EMBER_LINK_VERSION)) {
        cJSON_Delete(body);
        s_enrollment_state = "connection_error";
        goto done;
    }
    int status;
    s_enrollment_state = "registering";
    s_state = "enrolling";
    // Fresh candidate credential goes only to its pinned enrollment origin.
    // Existing device Authorization is never attached to bootstrap requests.
    cJSON *response = https_post(s_enrollment.candidate.api_base, NULL,
                                "v1/device/enroll", body, &status);
    cJSON_Delete(body);
    uint64_t version, generation;
    const char *session = string(response, "sessionId"), *id = string(response, "deviceId");
    const char *host = string(response, "downloadHost");
    if (response && link_json_u64(response, "protocolVersion", 1, 1, &version) &&
        session && !strcmp(session, s_enrollment.session_id) &&
        id && !strcmp(id, s_enrollment.candidate.device_id) &&
        host && !strcmp(host, s_enrollment.candidate.download_host) &&
        cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(response, "claimed")) &&
        link_json_u64(response, "ownershipGeneration", 1, 9007199254740991ULL, &generation)) {
        esp_err_t err = save("config", &s_enrollment.candidate, sizeof(s_config));
        if (err == ESP_OK) {
            s_config = s_enrollment.candidate;
            cloud_enrollment_t cleared = {.schema = 1};
            if (enrollment_save(&cleared) == ESP_OK) {
                s_enrollment_state = "linked";
                s_state = "waiting_for_wifi";
                delay = 1;
            }
        } else {
            s_enrollment_fault = true;
            s_enrollment_state = "storage_error";
        }
    } else if (status == 400 || status == 401 || status == 403 || status == 404 ||
               status == 409 || status == 410) {
        // Never discard the candidate on rejection: a prior response may have
        // been lost. A fresh ticket can resume with the same device credential.
        (void)enrollment_pause(status == 410 ? "expired" : status == 409 ? "blocked" : "rejected");
        delay = 60;
    } else {
        s_enrollment_state = status == 429 ? "rate_limited" : response ? "invalid_response" : "connection_error";
    }
    cJSON_Delete(response);
done:
    operation_end();
    return delay;
}

static const char *error_code(esp_err_t err)
{
    switch (err) {
    case ESP_ERR_INVALID_CRC:
        return "checksum_mismatch";
    case ESP_ERR_NO_MEM:
        return "insufficient_storage";
    case ESP_ERR_TIMEOUT:
        return "transfer_expired";
    case ESP_ERR_INVALID_SIZE:
        return "size_mismatch";
    default:
        return "download_failed";
    }
}

static esp_err_t download(const char *url, time_t expires, bool *commit_started)
{
    *commit_started = false;
    esp_http_client_config_t cfg = {.url = url,
                                    .timeout_ms = DOWNLOAD_TIMEOUT_MS,
                                    .crt_bundle_attach = esp_crt_bundle_attach,
                                    .disable_auto_redirect = true,
                                    .buffer_size = 2048,
                                    .buffer_size_tx = 4096};
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client)
        return ESP_ERR_NO_MEM;
    // No device Authorization header is attached to the S3 request.
    esp_err_t err = esp_http_client_open(client, 0);
    link_file_write_t writer = {0};
    bool begun = false;
    uint8_t *buf = NULL;
    int64_t started = esp_timer_get_time();
    if (err != ESP_OK)
        goto cleanup;
    int64_t length = esp_http_client_fetch_headers(client);
    if (esp_http_client_get_status_code(client) != 200 || length != (int64_t)s_receipt.size) {
        err = ESP_ERR_INVALID_SIZE;
        goto cleanup;
    }
    err = link_file_begin(&writer, s_receipt.filename, (size_t)s_receipt.size);
    if (err != ESP_OK)
        goto cleanup;
    begun = true;
    buf = malloc(4096);
    if (!buf) {
        err = ESP_ERR_NO_MEM;
        goto cleanup;
    }
    while (s_receipt.bytes < s_receipt.size) {
        if (time(NULL) >= expires || esp_timer_get_time() - started > TRANSFER_LIMIT_US) {
            err = ESP_ERR_TIMEOUT;
            goto cleanup;
        }
        size_t want = s_receipt.size - s_receipt.bytes;
        if (want > 4096)
            want = 4096;
        int n = esp_http_client_read(client, (char *)buf, (int)want);
        if (n <= 0) {
            err = ESP_FAIL;
            goto cleanup;
        }
        err = link_file_write(&writer, buf, (size_t)n);
        if (err != ESP_OK)
            goto cleanup;
        s_receipt.bytes += (size_t)n;
        display_progress(s_receipt.bytes);
    }
    if (!esp_http_client_is_complete_data_received(client)) {
        err = ESP_ERR_INVALID_SIZE;
        goto cleanup;
    }
    if (time(NULL) >= expires) {
        err = ESP_ERR_TIMEOUT;
        goto cleanup;
    }
    *commit_started = true;
    err = link_file_finish(&writer, s_receipt.sha256);
    begun = false;
    if (err == ESP_ERR_INVALID_CRC || err == ESP_ERR_INVALID_SIZE)
        *commit_started = false;
cleanup:
    if (begun)
        link_file_abort(&writer);
    free(buf);
    esp_http_client_cleanup(client);
    return err;
}

static void run_job(const cJSON *job, uint64_t generation)
{
    link_receipt_t next;
    time_t expiry;
    if (!link_job_parse(job, generation, s_config.download_host, time(NULL), &next, &expiry)) {
        s_state = "invalid_job";
        return;
    }
    // Last receipt is retained even after acknowledgement to reject replay.
    if (!strcmp(next.job_id, s_receipt.job_id)) {
        s_state = "duplicate_job";
        return;
    }
    if (unsettled() || !operation_begin()) {
        s_state = "busy";
        return;
    }
    if (save("receipt", &next, sizeof(next)) != ESP_OK) {
        s_state = "journal_error";
        operation_end();
        return;
    }
    s_receipt = next;
    s_state = "delivering";
    publish_status();
    display_begin(false, true, s_receipt.filename, s_receipt.size);
    esp_err_t err = storage_acquire();
    bool mounted = err == ESP_OK, commit = false;
    if (mounted) {
        led_set(LED_TRANSFER);
        err = download(string(job, "downloadUrl"), expiry, &commit);
        esp_err_t release = storage_release();
        if (release != ESP_OK) {
            err = release;
            commit = true;
        }
    }
    strcpy(s_receipt.state, err == ESP_OK ? "done" : commit ? "needs_reconciliation" : "failed");
    if (err != ESP_OK)
        snprintf(s_receipt.error, sizeof(s_receipt.error), "%s",
                 commit ? "storage_failed" : error_code(err));
    if (save("receipt", &s_receipt, sizeof(s_receipt)) != ESP_OK) {
        strcpy(s_receipt.state, "needs_reconciliation");
        strcpy(s_receipt.error, "journal_error");
        // The persisted delivering receipt makes reboot conservative too.
    }
    display_finish(!strcmp(s_receipt.state, "done"), err == ESP_ERR_NO_MEM ? "Card full" : "Transfer failed");
    led_set(err == ESP_OK ? LED_READY : LED_ERROR);
    s_state = "awaiting_receipt_ack";
    operation_end();
}

static void acknowledge(const cJSON *ack)
{
    if (!link_receipt_ack_matches(&s_receipt, ack))
        return;
    link_receipt_t next = s_receipt;
    next.acknowledged = true;
    if (save("receipt", &next, sizeof(next)) == ESP_OK)
        s_receipt = next;
}

static unsigned poll_cloud(void)
{
    cJSON *body = cJSON_CreateObject();
    cJSON_AddNumberToObject(body, "protocolVersion", 1);
    cJSON_AddStringToObject(body, "deviceId", s_config.device_id);
    cJSON_AddStringToObject(body, "firmwareVersion", EMBER_LINK_VERSION);
    if (s_setup.secret[0] && esp_timer_get_time() >= s_setup.expires_us) {
        memset(s_setup.secret, 0, sizeof(s_setup.secret));
        s_setup.state = "expired";
    }
    firmware_update_add_poll(body);
    display_cloud_add_poll(body);
    cJSON_AddBoolToObject(body, "readyForSettings", display_settings_available() && !unsettled() && !s_setup.secret[0]);
    cJSON_AddBoolToObject(body, "readyForJob", !unsettled() && !s_setup.secret[0]);
    if (s_setup.secret[0]) {
        cJSON *setup = cJSON_AddObjectToObject(body, "setup");
        cJSON_AddStringToObject(setup, "sessionId", s_setup.session_id);
        cJSON_AddStringToObject(setup, "secret", s_setup.secret);
    }
    if (s_receipt.job_id[0])
        cJSON_AddItemToObject(body, "receipt", receipt_json());
    uint64_t total = 0, free = 0;
    storage_cached_stats(&total, &free);
    cJSON *storage = cJSON_AddObjectToObject(body, "storage");
    cJSON_AddNumberToObject(storage, "totalBytes", (double)total);
    cJSON_AddNumberToObject(storage, "freeBytes", (double)free);
    int status;
    cJSON *response = api_post("v1/device/poll", body, &status);
    cJSON_Delete(body);
    if (status == 401 || status == 403) {
        s_config.enabled = false;
        (void)save("config", &s_config, sizeof(s_config));
        s_state = "authorization_failed";
        memset(s_setup.secret, 0, sizeof(s_setup.secret));
        s_setup.state = "authorization_failed";
        cJSON_Delete(response);
        return 60;
    }
    uint64_t version, generation, delay = 10;
    if (!response || !link_json_u64(response, "protocolVersion", 1, 1, &version)) {
        s_state = status == 429 ? "rate_limited" : "connection_error";
        cJSON_Delete(response);
        return 0;
    }
    (void)link_json_u64(response, "nextPollSeconds", 5, 300, &delay);
    const cJSON *setup_result = cJSON_GetObjectItemCaseSensitive(response, "setupResult");
    const char *setup_id = string(setup_result, "sessionId");
    const char *setup_state = string(setup_result, "status");
    if (s_setup.secret[0] && setup_id && !strcmp(setup_id, s_setup.session_id) && setup_state) {
        if (!strcmp(setup_state, "linked")) s_setup.state = "linked";
        else if (!strcmp(setup_state, "blocked")) s_setup.state = "blocked";
        else if (!strcmp(setup_state, "expired")) s_setup.state = "expired";
        else if (!strcmp(setup_state, "invalid")) s_setup.state = "invalid";
        if (s_setup.state && strcmp(s_setup.state, "pending"))
            memset(s_setup.secret, 0, sizeof(s_setup.secret));
    }
    acknowledge(cJSON_GetObjectItemCaseSensitive(response, "receiptAck"));
    firmware_update_ack(cJSON_GetObjectItemCaseSensitive(response, "firmwareAck"));
    if (cJSON_IsObject(cJSON_GetObjectItemCaseSensitive(response, "settingsAck")) && operation_begin()) {
        (void)display_cloud_ack(cJSON_GetObjectItemCaseSensitive(response, "settingsAck"));
        operation_end();
    }
    if (!cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(response, "claimed"))) {
        s_state = "unclaimed";
    } else if (!link_json_u64(response, "ownershipGeneration", 1, 9007199254740991ULL,
                              &generation)) {
        s_state = "invalid_response";
    } else if (!unsettled() && !s_setup.secret[0] && cJSON_IsObject(cJSON_GetObjectItemCaseSensitive(response, "firmwareUpdate"))) {
        firmware_update_run(cJSON_GetObjectItemCaseSensitive(response, "firmwareUpdate"), generation, s_config.download_host);
        delay = 5;
    } else if (!unsettled() && !s_setup.secret[0] && display_settings_available() && cJSON_IsObject(cJSON_GetObjectItemCaseSensitive(response, "settingsUpdate"))) {
        if (operation_begin()) {
            esp_err_t err = display_cloud_run(cJSON_GetObjectItemCaseSensitive(response, "settingsUpdate"), generation, time(NULL));
            if (err == ESP_OK) display_apply_saved_settings();
            s_state = err == ESP_OK ? "awaiting_receipt_ack" : err == ESP_ERR_INVALID_ARG ? "invalid_settings" : "journal_error";
            operation_end();
        }
        delay = 5;
    } else if (!unsettled() && !s_setup.secret[0] && cJSON_IsObject(cJSON_GetObjectItemCaseSensitive(response, "job"))) {
        run_job(cJSON_GetObjectItemCaseSensitive(response, "job"), generation);
        delay = 5; // promptly deliver the receipt on the next request.
    } else {
        s_state = unsettled() ? "awaiting_receipt_ack" : "online";
    }
    cJSON_Delete(response);
    return (unsigned)delay;
}

static void worker(void *arg)
{
    (void)arg;
    unsigned backoff = 5;
    while (true) {
        unsigned delay = 5;
        if (xSemaphoreTake(s_session, 0) == pdTRUE) {
            if (!s_enrollment_fault && ((enrollment_pending() && s_enrollment.active) ||
                (s_config.enabled && config_valid(&s_config)))) {
                char ip[16];
                wifi_mgr_get_ip(ip);
                if (!strcmp(ip, "0.0.0.0") || wifi_mgr_in_setup())
                    s_state = "waiting_for_wifi";
                else {
                    if (!s_sntp_started) {
                        esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
                        s_sntp_started = esp_netif_sntp_init(&cfg) == ESP_OK;
                    }
                    if (time(NULL) < 1735689600)
                        s_state = "waiting_for_clock";
                    else {
                        delay = enrollment_pending() ? enroll_cloud() : poll_cloud();
                        if (!delay) {
                            delay = backoff;
                            backoff = backoff < 150 ? backoff * 2 : 300;
                        } else
                            backoff = 5;
                    }
                }
            }
            // A rejected credential disables polling, but still needs an error
            // indication rather than looking like an intentional local-only setup.
            display_cloud(!strcmp(s_state, "authorization_failed") ? DISPLAY_CLOUD_ERROR :
                          enrollment_pending() ? (s_enrollment.active && !s_enrollment_fault ? DISPLAY_CLOUD_CONNECTING : DISPLAY_CLOUD_ERROR) :
                          !s_config.enabled ? DISPLAY_CLOUD_OFF :
                          !strcmp(s_state, "online") || !strcmp(s_state, "awaiting_receipt_ack") ? DISPLAY_CLOUD_ONLINE :
                          !strcmp(s_state, "unclaimed") ? DISPLAY_CLOUD_UNCLAIMED :
                          !strcmp(s_state, "connection_error") ||
                          !strcmp(s_state, "invalid_response") || !strcmp(s_state, "rate_limited") ||
                          !strcmp(s_state, "invalid_settings") || !strcmp(s_state, "journal_error") || !strcmp(s_state, "invalid_job") ? DISPLAY_CLOUD_ERROR : DISPLAY_CLOUD_CONNECTING);
            publish_status();
            xSemaphoreGive(s_session);
        }
        // A USB setup request wakes a worker that is backing off after a network failure.
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(delay * 1000 + (esp_random() % 1000)));
    }
}

esp_err_t cloud_init(void)
{
    s_session = xSemaphoreCreateMutex();
    if (!s_session)
        return ESP_ERR_NO_MEM;
    esp_err_t err = cloud_settings_migrate();
    if (err != ESP_OK) return err;
    err = firmware_update_init();
    if (err != ESP_OK) return err;
    err = read_blob("config", &s_config, sizeof(s_config));
    if (err != ESP_OK && err != ESP_ERR_NOT_FOUND)
        return err;
    if (err == ESP_OK && !config_valid(&s_config))
        return ESP_ERR_INVALID_STATE;
    err = read_blob("receipt", &s_receipt, sizeof(s_receipt));
    if (err != ESP_OK && err != ESP_ERR_NOT_FOUND)
        return err;
    if (err == ESP_OK) {
        if (s_receipt.schema != CLOUD_SCHEMA ||
            !memchr(s_receipt.job_id, 0, sizeof(s_receipt.job_id)) ||
            !memchr(s_receipt.attempt_id, 0, sizeof(s_receipt.attempt_id)) ||
            !memchr(s_receipt.filename, 0, sizeof(s_receipt.filename)) ||
            !memchr(s_receipt.sha256, 0, sizeof(s_receipt.sha256)) ||
            !memchr(s_receipt.state, 0, sizeof(s_receipt.state)) ||
            !memchr(s_receipt.error, 0, sizeof(s_receipt.error)) ||
            !link_id_valid(s_receipt.job_id) || !link_id_valid(s_receipt.attempt_id))
            return ESP_ERR_INVALID_STATE;
        if (!strcmp(s_receipt.state, "delivering")) {
            strcpy(s_receipt.state, "needs_reconciliation");
            strcpy(s_receipt.error, "interrupted");
            s_receipt.acknowledged = false;
            if (save("receipt", &s_receipt, sizeof(s_receipt)) != ESP_OK)
                return ESP_FAIL;
        }
    }
    memset(&s_enrollment, 0, sizeof(s_enrollment));
    s_enrollment_fault = false;
    s_enrollment_state = "idle";
    err = read_blob("enrollment", &s_enrollment, sizeof(s_enrollment));
    if (err != ESP_OK && err != ESP_ERR_NOT_FOUND) return err;
    if (err == ESP_OK && !enrollment_valid(&s_enrollment)) return ESP_ERR_INVALID_STATE;
    if (enrollment_pending()) {
        if (config_valid(&s_config)) {
            // Power loss after saving config but before clearing the journal.
            if (!enrollment_matches_config()) return ESP_ERR_INVALID_STATE;
            cloud_enrollment_t cleared = {.schema = 1};
            err = enrollment_save(&cleared);
            if (err != ESP_OK) return err;
            s_enrollment_state = "linked";
        } else {
            s_enrollment_state = s_enrollment.active ? "pending" : "retry_required";
            s_enrollment_deadline = esp_timer_get_time() + 600LL * 1000000;
        }
    }
    memset(&s_setup, 0, sizeof(s_setup));
    s_state = s_config.enabled || (enrollment_pending() && s_enrollment.active) ? "waiting_for_wifi" : "disabled";
    publish_status();
    return ESP_OK;
}

esp_err_t cloud_start(void)
{
    if (s_started)
        return ESP_ERR_INVALID_STATE;
    if (xTaskCreate(worker, "link_cloud", 12288, NULL, 4, &s_worker) != pdPASS)
        return ESP_ERR_NO_MEM;
    s_started = true;
    return ESP_OK;
}

esp_err_t cloud_configure(const cJSON *request)
{
    const char *base = string(request, "apiBaseUrl"), *host = string(request, "downloadHost");
    const char *id = string(request, "deviceId"), *token = string(request, "token");
    if (!link_api_base_valid(base) || !link_host_valid(host) || !link_id_valid(id) ||
        !link_token_valid(token))
        return ESP_ERR_INVALID_ARG;
    if (xSemaphoreTake(s_session, 0) != pdTRUE)
        return ESP_ERR_INVALID_STATE;
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (!enrollment_pending() && !s_enrollment_fault && !unsettled() && operation_begin()) {
        cloud_config_t next = {.schema = CLOUD_SCHEMA, .enabled = false};
        strcpy(next.api_base, base);
        strcpy(next.download_host, host);
        strcpy(next.device_id, id);
        strcpy(next.token, token);
        err = save("config", &next, sizeof(next));
        if (err == ESP_OK) {
            s_config = next;
            memset(&s_setup, 0, sizeof(s_setup));
            s_state = "disabled";
        }
        memset(&next, 0, sizeof(next));
        operation_end();
    }
    publish_status();
    xSemaphoreGive(s_session);
    return err;
}

const char *cloud_enroll_error_code(cloud_enroll_result_t result)
{
    switch (result) {
    case CLOUD_ENROLL_OK: return NULL;
    case CLOUD_ENROLL_BUSY: return "busy";
    case CLOUD_ENROLL_ALREADY_CONFIGURED: return "already_configured";
    case CLOUD_ENROLL_SERVICE_MISMATCH: return "service_mismatch";
    case CLOUD_ENROLL_STORAGE_ERROR: return "storage_error";
    default: return "invalid_enrollment";
    }
}

const char *cloud_enroll_error_message(cloud_enroll_result_t result)
{
    switch (result) {
    case CLOUD_ENROLL_OK: return NULL;
    case CLOUD_ENROLL_BUSY: return "Link is busy; wait for the current operation or receipt to finish, then retry";
    case CLOUD_ENROLL_ALREADY_CONFIGURED: return "Link already has a cloud identity; use account linking instead of enrollment";
    case CLOUD_ENROLL_SERVICE_MISMATCH: return "This enrollment belongs to another cloud service; resume with the original service or contact support";
    case CLOUD_ENROLL_STORAGE_ERROR: return "Link could not access or save its settings; restart Link before retrying";
    default: return "Check the enrollment ticket, session and service configuration";
    }
}

cloud_enroll_result_t cloud_enroll(const cJSON *request, const char *serial)
{
    const char *base = string(request, "apiBaseUrl"), *host = string(request, "downloadHost");
    const char *session = string(request, "sessionId"), *ticket = string(request, "ticket");
    if (!link_api_base_valid(base) || !link_host_valid(host) ||
        !link_id_valid(session) || !link_token_valid(ticket) || !serial || strlen(serial) != 12 ||
        strspn(serial, "0123456789ABCDEF") != 12) return CLOUD_ENROLL_INVALID_REQUEST;
    if (xSemaphoreTake(s_session, 0) != pdTRUE) return CLOUD_ENROLL_BUSY;
    cloud_enroll_result_t result = CLOUD_ENROLL_BUSY;
    if (s_enrollment_fault || !display_settings_available()) result = CLOUD_ENROLL_STORAGE_ERROR;
    else if (config_valid(&s_config)) result = CLOUD_ENROLL_ALREADY_CONFIGURED;
    else if (!unsettled() && operation_begin()) {
        cloud_enrollment_t next = s_enrollment;
        if (enrollment_pending()) {
            // Never redirect a saved credential to another service.
            if (strcmp(base, next.candidate.api_base) || strcmp(host, next.candidate.download_host)) {
                result = CLOUD_ENROLL_SERVICE_MISMATCH;
                goto done;
            }
            if (!strcmp(session, next.session_id) && strcmp(ticket, next.ticket)) {
                result = CLOUD_ENROLL_INVALID_REQUEST;
                goto done;
            }
            if (next.active && !strcmp(session, next.session_id)) {
                result = CLOUD_ENROLL_OK; // Duplicate USB request does not extend deadline.
                goto done;
            }
        } else {
            memset(&next, 0, sizeof(next));
            next.schema = 1;
            next.candidate.schema = CLOUD_SCHEMA;
            next.candidate.enabled = true;
            strcpy(next.candidate.api_base, base);
            strcpy(next.candidate.download_host, host);
            snprintf(next.candidate.device_id, sizeof(next.candidate.device_id), "link-%s", serial);
            for (char *c = next.candidate.device_id; *c; ++c)
                if (*c >= 'A' && *c <= 'F') *c += 'a' - 'A';
        }
        next.active = true;
        strcpy(next.session_id, session);
        strcpy(next.ticket, ticket);
        result = enrollment_save(&next) == ESP_OK ? CLOUD_ENROLL_OK : CLOUD_ENROLL_STORAGE_ERROR;
        if (result == CLOUD_ENROLL_OK) {
            s_enrollment_state = "pending";
            s_enrollment_deadline = esp_timer_get_time() + 600LL * 1000000;
            s_state = "waiting_for_wifi";
            if (s_worker) xTaskNotifyGive(s_worker);
        }
done:
        memset(&next, 0, sizeof(next));
        operation_end();
    }
    publish_status();
    xSemaphoreGive(s_session);
    return result;
}

esp_err_t cloud_claim(const cJSON *request)
{
    const char *session = string(request, "sessionId"), *secret = string(request, "secret");
    if (!link_id_valid(session) || !link_token_valid(secret)) return ESP_ERR_INVALID_ARG;
    if (xSemaphoreTake(s_session, 0) != pdTRUE) return ESP_ERR_INVALID_STATE;
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (config_valid(&s_config) && !enrollment_pending() && !s_enrollment_fault && !unsettled() && operation_begin()) {
        // Idempotent USB retry cannot extend a session's local lifetime.
        if (!strcmp(session, s_setup.session_id) && s_setup.state) {
            err = !s_setup.secret[0] || !strcmp(secret, s_setup.secret) ? ESP_OK : ESP_ERR_INVALID_ARG;
        } else {
            cloud_config_t next = s_config;
            next.enabled = true;
            err = save("config", &next, sizeof(next));
            if (err == ESP_OK) {
                s_config = next;
                memset(&s_setup, 0, sizeof(s_setup));
                strcpy(s_setup.session_id, session);
                strcpy(s_setup.secret, secret);
                s_setup.expires_us = esp_timer_get_time() + 600LL * 1000000;
                s_setup.state = "pending";
                s_state = "waiting_for_wifi";
                if (s_worker) xTaskNotifyGive(s_worker);
            }
        }
        operation_end();
    }
    publish_status();
    xSemaphoreGive(s_session);
    return err;
}

esp_err_t cloud_set_enabled(bool enabled)
{
    if (xSemaphoreTake(s_session, 0) != pdTRUE)
        return ESP_ERR_INVALID_STATE;
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (s_enrollment_fault) {
        err = ESP_ERR_INVALID_STATE;
    } else if (enrollment_pending()) {
        // Disable/reset pauses but retains the credential for safe recovery.
        // Resuming requires an explicit cloud_enroll USB request.
        if (!enabled) err = enrollment_pause("paused");
    } else if (config_valid(&s_config) || !enabled) {
        if (!config_valid(&s_config)) {
            s_state = "disabled";
            err = ESP_OK;
        } else {
            cloud_config_t next = s_config;
            next.enabled = enabled;
            err = save("config", &next, sizeof(next));
            if (err == ESP_OK) {
                s_config = next;
                s_state = enabled ? "waiting_for_wifi" : "disabled";
                if (!enabled) memset(&s_setup, 0, sizeof(s_setup));
            }
        }
    }
    publish_status();
    xSemaphoreGive(s_session);
    return err;
}

esp_err_t cloud_disable_for_reset(void)
{
    return cloud_set_enabled(false);
}

cJSON *cloud_status(void)
{
    bool fresh = xSemaphoreTake(s_session, 0) == pdTRUE;
    if (fresh) publish_status();
    cloud_status_snapshot_t snapshot = status_snapshot();
    // Build JSON outside the cache critical section; never inspect mutable
    // session-owned fields when the network worker holds the session lock.
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "protocolVersion", 1);
    cJSON_AddBoolToObject(o, "busy", !fresh);
    cJSON_AddBoolToObject(o, "cached", !fresh);
    int64_t age = esp_timer_get_time() - snapshot.captured_us;
    cJSON_AddNumberToObject(o, "snapshotAgeMs", age > 0 ? (double)(age / 1000) : 0);
    cJSON *network = cJSON_AddObjectToObject(o, "network");
    cJSON_AddStringToObject(network, "stage", snapshot.network.stage ? snapshot.network.stage : "idle");
    cJSON_AddNumberToObject(network, "httpStatus", snapshot.network.http_status);
    cJSON_AddNumberToObject(network, "error", snapshot.network.error);
    cJSON_AddNumberToObject(network, "socketErrno", snapshot.network.socket_errno);
    cJSON_AddNumberToObject(network, "transportError", snapshot.network.transport_error);
    cJSON_AddNumberToObject(network, "tlsError", snapshot.network.tls_error);
    cJSON_AddNumberToObject(network, "tlsFlags", snapshot.network.tls_flags);
    cJSON_AddNumberToObject(o, "enrollmentProtocolVersion", 1);
    cJSON_AddStringToObject(o, "enrollmentState", snapshot.enrollment_state ? snapshot.enrollment_state : "idle");
    cJSON_AddBoolToObject(o, "enrollmentPending", snapshot.enrollment_pending);
    if (snapshot.enrollment_pending) {
        cJSON_AddStringToObject(o, "enrollmentSessionId", snapshot.enrollment_session_id);
        cJSON_AddStringToObject(o, "enrollmentDeviceId", snapshot.enrollment_device_id);
    }
    cJSON_AddBoolToObject(o, "configured", snapshot.configured);
    cJSON_AddStringToObject(o, "setupState", snapshot.setup_state ? snapshot.setup_state : "idle");
    if (snapshot.setup_session_id[0]) cJSON_AddStringToObject(o, "setupSessionId", snapshot.setup_session_id);
    cJSON_AddBoolToObject(o, "enabled", snapshot.enabled);
    cJSON_AddStringToObject(o, "state", snapshot.state ? snapshot.state : "disabled");
    if (snapshot.configured) cJSON_AddStringToObject(o, "deviceId", snapshot.device_id);
    if (fresh) {
        firmware_update_add_poll(o);
        if (s_receipt.job_id[0]) cJSON_AddItemToObject(o, "receipt", receipt_json());
        xSemaphoreGive(s_session);
    }
    return o;
}
