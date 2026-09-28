// Execute the real cloud worker's poll/download/receipt logic. Only ESP-IDF I/O
// is substituted; parser and state transitions are the production source.
#include "../../firmware/main/cloud.c"
#include <assert.h>

static cloud_config_t saved_config;
static link_receipt_t saved_receipt;
static bool have_config, have_receipt, fail_save, gate;
static int starts, releases, download_status = 200, api_status = 200;
static esp_err_t finish_result = ESP_OK, open_result = ESP_OK;
static char *reply;
static char last_request[4096];
static unsigned mutex_count;
static int mutexes[8];
struct test_http {
    bool api;
    int offset;
    const char *body;
    int len;
    bool authorized;
};

static int display_starts, display_completions, display_successes;
static uint64_t display_bytes;
void display_begin(bool firmware, bool cloud, const char *name, uint64_t total)
{
    assert(gate && have_receipt && !strcmp(saved_receipt.state, "delivering"));
    assert(!firmware && cloud && !strcmp(name, "rose.pes") && total == 3);
    ++display_starts;
    display_bytes = 0;
}
void display_progress(uint64_t bytes)
{
    assert(gate && bytes >= display_bytes && bytes <= 3);
    display_bytes = bytes;
}
void display_finish(bool success, const char *error)
{
    assert(gate && releases == display_starts);
    assert(success == !strcmp(s_receipt.state, "done"));
    if (success) {
        assert(!strcmp(saved_receipt.state, "done") && display_bytes == 3);
        ++display_successes;
    } else assert(error && *error);
    ++display_completions;
}
void display_cloud(display_cloud_t state) { (void)state; }

SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    assert(mutex_count < 8);
    return &mutexes[mutex_count++];
}
int xSemaphoreTake(SemaphoreHandle_t h, TickType_t timeout)
{
    (void)timeout;
    if (*h)
        return 0;
    *h = 1;
    return 1;
}
void xSemaphoreGive(SemaphoreHandle_t h)
{
    assert(*h);
    *h = 0;
}
int xTaskCreate(void (*f)(void *), const char *n, unsigned stack, void *arg, unsigned priority,
                void *handle)
{
    (void)f;
    (void)n;
    (void)stack;
    (void)arg;
    (void)priority;
    if (handle) *(TaskHandle_t *)handle = (TaskHandle_t)1;
    return pdPASS;
}
void vTaskDelay(TickType_t t)
{
    (void)t;
}
uint32_t ulTaskNotifyTake(int clear, TickType_t wait) { (void)clear; (void)wait; return 0; }
void xTaskNotifyGive(TaskHandle_t handle) { assert(handle); }
uint32_t esp_random(void)
{
    return 0;
}
int64_t esp_timer_get_time(void)
{
    return 1000000;
}
esp_err_t esp_netif_sntp_init(const esp_sntp_config_t *c)
{
    (void)c;
    return ESP_OK;
}
esp_err_t esp_crt_bundle_attach(void *x)
{
    (void)x;
    return ESP_OK;
}
void wifi_mgr_get_ip(char out[16])
{
    strcpy(out, "192.168.1.5");
}
bool wifi_mgr_in_setup(void)
{
    return false;
}
bool operation_begin(void)
{
    if (gate)
        return false;
    gate = true;
    return true;
}
void operation_end(void)
{
    assert(gate);
    gate = false;
}
esp_err_t storage_acquire(void)
{
    assert(gate);
    return ESP_OK;
}
esp_err_t storage_release(void)
{
    ++releases;
    return ESP_OK;
}
void storage_cached_stats(uint64_t *total, uint64_t *free)
{
    *total = 10000000;
    *free = 9000000;
}
void led_set(led_state_t s)
{
    (void)s;
}
esp_err_t link_file_begin(link_file_write_t *w, const char *name, size_t size)
{
    (void)w;
    assert(gate && have_receipt && !strcmp(saved_receipt.state, "delivering"));
    assert(!strcmp(name, "rose.pes") && size == 3);
    ++starts;
    return ESP_OK;
}
esp_err_t link_file_write(link_file_write_t *w, const void *data, size_t n)
{
    (void)w;
    assert(n == 3 && !memcmp(data, "abc", 3));
    return ESP_OK;
}
esp_err_t link_file_finish(link_file_write_t *w, const char *hash)
{
    (void)w;
    assert(link_sha256_valid(hash));
    return finish_result;
}
void link_file_abort(link_file_write_t *w)
{
    (void)w;
}
esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *h)
{
    (void)mode;
    assert(!strcmp(name, "link_cloud"));
    *h = 1;
    return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *p, size_t n)
{
    (void)h;
    if (fail_save)
        return ESP_FAIL;
    if (!strcmp(key, "config")) {
        assert(n == sizeof(saved_config));
        memcpy(&saved_config, p, n);
        have_config = true;
    } else {
        assert(!strcmp(key, "receipt") && n == sizeof(saved_receipt));
        memcpy(&saved_receipt, p, n);
        have_receipt = true;
    }
    return ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *p, size_t *n)
{
    (void)h;
    if (!strcmp(key, "config")) {
        if (!have_config)
            return ESP_ERR_NVS_NOT_FOUND;
        assert(*n == sizeof(saved_config));
        memcpy(p, &saved_config, *n);
    } else {
        if (!have_receipt)
            return ESP_ERR_NVS_NOT_FOUND;
        assert(*n == sizeof(saved_receipt));
        memcpy(p, &saved_receipt, *n);
    }
    return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h)
{
    (void)h;
    return ESP_OK;
}
void nvs_close(nvs_handle_t h)
{
    (void)h;
}

esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *c)
{
    assert(c->crt_bundle_attach == esp_crt_bundle_attach && c->disable_auto_redirect);
    struct test_http *h = calloc(1, sizeof(*h));
    assert(h);
    h->api = c->method == HTTP_METHOD_POST;
    assert(h->api ? !strcmp(c->url, "https://api.example.com/v1/device/poll")
                  : !strcmp(c->url, "https://files.example.com/design"));
    h->body = h->api ? reply : "abc";
    h->len = (int)strlen(h->body);
    return h;
}
esp_err_t esp_http_client_set_header(esp_http_client_handle_t h, const char *key, const char *value)
{
    if (!strcmp(key, "Authorization")) {
        assert(h->api && !strncmp(value, "Bearer ", 7));
        h->authorized = true;
    }
    return ESP_OK;
}
esp_err_t esp_http_client_open(esp_http_client_handle_t h, int len)
{
    (void)len;
    assert(h->api == h->authorized);
    last_request[0] = 0;
    return open_result;
}
int esp_http_client_get_errno(esp_http_client_handle_t h) { (void)h; return 113; }
esp_err_t esp_http_client_get_and_clear_last_tls_error(esp_http_client_handle_t h, int *error, int *flags)
{ (void)h; *error = -123; *flags = 8; return ESP_OK; }
int esp_http_client_write(esp_http_client_handle_t h, const char *p, int n)
{
    assert(h->api && n < (int)sizeof(last_request));
    memcpy(last_request, p, n);
    last_request[n] = 0;
    return n;
}
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t h)
{
    return h->len;
}
int esp_http_client_get_status_code(esp_http_client_handle_t h)
{
    return h->api ? api_status : download_status;
}
int esp_http_client_read(esp_http_client_handle_t h, char *buf, int n)
{
    int left = h->len - h->offset;
    if (n > left)
        n = left;
    memcpy(buf, h->body + h->offset, n);
    h->offset += n;
    return n;
}
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t h)
{
    return h->offset == h->len;
}
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t h)
{
    free(h);
    return ESP_OK;
}

static void response(const char *id, bool ack)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "protocolVersion", 1);
    cJSON_AddBoolToObject(o, "claimed", true);
    cJSON_AddNumberToObject(o, "ownershipGeneration", 2);
    if (ack)
        cJSON_AddItemToObject(o, "receiptAck", receipt_json());
    if (id) {
        cJSON *j = cJSON_AddObjectToObject(o, "job");
        cJSON_AddStringToObject(j, "type", "download");
        cJSON_AddStringToObject(j, "jobId", id);
        cJSON_AddStringToObject(j, "attemptId", "attempt-1");
        cJSON_AddStringToObject(j, "filename", "rose.pes");
        cJSON_AddStringToObject(j, "sha256",
                                "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
        cJSON_AddStringToObject(j, "downloadUrl", "https://files.example.com/design");
        cJSON_AddNumberToObject(j, "size", 3);
        cJSON_AddNumberToObject(j, "ownershipGeneration", 2);
        cJSON_AddNumberToObject(j, "expiresAt", (double)time(NULL) + 300);
    }
    free(reply);
    reply = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
}

int main(void)
{
    assert(cloud_init() == ESP_OK);
    cJSON *cfg =
        cJSON_Parse("{\"apiBaseUrl\":\"https://api.example.com/"
                    "\",\"downloadHost\":\"files.example.com\",\"deviceId\":\"device-1\",\"token\":"
                    "\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"}");
    assert(cloud_configure(cfg) == ESP_OK && !s_config.enabled);
    assert(cloud_set_enabled(true) == ESP_OK);
    cJSON *status = cloud_status();
    char *text = cJSON_PrintUnformatted(status);
    assert(!strstr(text, "aaaa") && !strstr(text, "token"));
    free(text);
    cJSON_Delete(status);
    response(NULL, false);
    open_result = ESP_FAIL;
    poll_cloud();
    assert(!strcmp(s_network.stage, "connect") && s_network.error == ESP_FAIL);
    assert(s_network.socket_errno == 113 && s_network.tls_error == -123 && s_network.tls_flags == 8);
    status = cloud_status(); text = cJSON_PrintUnformatted(status);
    assert(strstr(text, "socketErrno") && !strstr(text, "aaaa") && !strstr(text, "https://"));
    free(text); cJSON_Delete(status);
    open_result = ESP_OK;
    poll_cloud();
    assert(!strcmp(s_network.stage, "complete") && s_network.error == 0);
    assert(s_network.socket_errno == 0 && s_network.tls_error == 0 && s_network.tls_flags == 0);
    // Consumer setup forwards only a short-lived USB proof. Device credentials
    // never appear in status; duplicate setup commands do not extend lifetime.
    cJSON *setup = cJSON_Parse("{\"sessionId\":\"setup-one\",\"secret\":\"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\"}");
    assert(cloud_claim(setup) == ESP_OK);
    int64_t expires = s_setup.expires_us;
    assert(cloud_claim(setup) == ESP_OK && s_setup.expires_us == expires);
    status = cloud_status(); text = cJSON_PrintUnformatted(status);
    assert(!strstr(text, "bbbb") && !strstr(text, "aaaa") && strstr(text, "pending"));
    free(text); cJSON_Delete(status);
    response(NULL, false); poll_cloud();
    assert(strstr(last_request, "setup-one") && strstr(last_request, "bbbb"));
    assert(strstr(last_request, "\"readyForJob\":false"));
    free(reply);
    reply = strdup("{\"protocolVersion\":1,\"claimed\":true,\"ownershipGeneration\":2,\"setupResult\":{\"sessionId\":\"wrong-id\",\"status\":\"linked\"}}");
    poll_cloud(); assert(s_setup.secret[0]);
    free(reply);
    reply = strdup("{\"protocolVersion\":1,\"claimed\":true,\"ownershipGeneration\":2,\"setupResult\":{\"sessionId\":\"setup-one\",\"status\":\"linked\"}}");
    poll_cloud(); assert(!s_setup.secret[0] && !strcmp(s_setup.state, "linked"));
    cJSON_ReplaceItemInObject(setup, "sessionId", cJSON_CreateString("setup-two"));
    fail_save = true; assert(cloud_claim(setup) == ESP_FAIL && !s_setup.secret[0]); fail_save = false;
    assert(cloud_claim(setup) == ESP_OK);
    s_setup.expires_us = 1; response(NULL, false); poll_cloud();
    assert(!s_setup.secret[0] && !strcmp(s_setup.state, "expired"));
    cJSON_ReplaceItemInObject(setup, "sessionId", cJSON_CreateString("setup-three"));
    assert(cloud_claim(setup) == ESP_OK);
    assert(cloud_set_enabled(false) == ESP_OK && !s_setup.secret[0]);
    assert(cloud_set_enabled(true) == ESP_OK);
    cJSON_Delete(setup);
    response("job-1", false);
    poll_cloud();
    assert(starts == 1 && releases == 1 && !strcmp(saved_receipt.state, "done"));
    assert(display_starts == 1 && display_completions == 1 && display_successes == 1);
    // Unacknowledged receipt blocks new jobs and credential reconfiguration.
    response("job-2", false);
    poll_cloud();
    assert(starts == 1);
    assert(cloud_configure(cfg) == ESP_ERR_INVALID_STATE);
    // Acknowledgement allows new work; duplicate acknowledged job does not run.
    response("job-1", true);
    poll_cloud();
    assert(starts == 1 && saved_receipt.acknowledged);
    response("job-2", false);
    poll_cloud();
    assert(starts == 2);
    // Another operation holds the gate: no journal or download side effects.
    response("job-3", true);
    gate = true;
    poll_cloud();
    gate = false;
    assert(starts == 2 && !strcmp(saved_receipt.job_id, "job-2"));
    // Durable receipt must be recorded before acquiring/writing the card.
    fail_save = true;
    response("job-3", false);
    poll_cloud();
    assert(starts == 2);
    fail_save = false;
    // Digest rejection is a known failure, commit I/O failure is ambiguous.
    finish_result = ESP_ERR_INVALID_CRC;
    poll_cloud();
    assert(starts == 3 && !strcmp(saved_receipt.state, "failed"));
    finish_result = ESP_FAIL;
    response("job-4", true);
    poll_cloud();
    assert(!strcmp(saved_receipt.state, "needs_reconciliation"));
    assert(display_starts == 4 && display_completions == 4 && display_successes == 2);
    response("job-5", true);
    poll_cloud();
    assert(starts == 4 && !saved_receipt.acknowledged);
    // Simulate reboot with a durable in-progress record; never replay it.
    strcpy(saved_receipt.state, "delivering");
    memset(&s_receipt, 0, sizeof(s_receipt));
    assert(cloud_init() == ESP_OK);
    assert(!strcmp(s_receipt.state, "needs_reconciliation") &&
           !strcmp(s_receipt.error, "interrupted"));
    response("job-5", false);
    poll_cloud();
    assert(starts == 4);
    // Auth failures pause polling persistently, and reset preserves the journal.
    api_status = 401;
    poll_cloud();
    assert(!saved_config.enabled && !s_config.enabled);
    assert(cloud_disable_for_reset() == ESP_OK && have_receipt);
    cJSON_Delete(cfg);
    free(reply);
    puts("cloud worker tests passed (TLS policy, auth, journal-before-write, replay, recovery)");
    return 0;
}

// Update subsystem is exercised with real OTA/NVS/HTTP faults in test_updates.c.
esp_err_t cloud_settings_migrate(void) { return ESP_OK; }
esp_err_t firmware_update_init(void) { return ESP_OK; }
bool firmware_update_pending(void) { return false; }
void firmware_update_add_poll(cJSON *body) { (void)body; }
void firmware_update_ack(const cJSON *ack) { (void)ack; }
void firmware_update_run(const cJSON *job, uint64_t gen, const char *host) { (void)job; (void)gen; (void)host; }
