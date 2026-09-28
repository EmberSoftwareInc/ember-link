#include "auth.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"

#include "wifi_mgr.h"

static const char *TAG = "auth";

#define PAIR_WINDOW_US (5LL * 60 * 1000 * 1000)
#define NVS_NAMESPACE "auth"
#define NVS_KEY "clients"

static auth_client_t s_clients[AUTH_MAX_CLIENTS];
static size_t s_count;
static int64_t s_window_until_us;
static SemaphoreHandle_t s_lock;

static void persist(void)
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) != ESP_OK) {
        ESP_LOGE(TAG, "could not open NVS; tokens will not survive reboot");
        return;
    }
    esp_err_t err = nvs_set_blob(nvs, NVS_KEY, s_clients, s_count * sizeof(s_clients[0]));
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "persist failed: %s", esp_err_to_name(err));
    }
}

esp_err_t auth_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    // Power-on opens the window: plugging the dongle in is the consent
    // gesture a headless device has instead of a confirmation dialog.
    s_window_until_us = esp_timer_get_time() + PAIR_WINDOW_US;

    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) {
        return ESP_OK; // first boot: nothing stored yet
    }
    size_t len = sizeof(s_clients);
    if (nvs_get_blob(nvs, NVS_KEY, s_clients, &len) == ESP_OK &&
        len % sizeof(s_clients[0]) == 0) {
        s_count = len / sizeof(s_clients[0]);
    }
    nvs_close(nvs);
    ESP_LOGI(TAG, "%u paired client(s)", (unsigned)s_count);
    return ESP_OK;
}

bool auth_pairing_open(void)
{
    return esp_timer_get_time() < s_window_until_us || wifi_mgr_in_setup();
}

void auth_open_window(void)
{
    s_window_until_us = esp_timer_get_time() + PAIR_WINDOW_US;
    ESP_LOGI(TAG, "pairing window open for 5 minutes");
}

esp_err_t auth_pair(const char *name, char out_token[AUTH_TOKEN_LEN + 1])
{
    if (!auth_pairing_open()) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_count >= AUTH_MAX_CLIENTS) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_NO_MEM;
    }

    auth_client_t *client = &s_clients[s_count];
    uint8_t raw[AUTH_TOKEN_LEN / 2];
    esp_fill_random(raw, sizeof(raw));
    for (size_t i = 0; i < sizeof(raw); i++) {
        snprintf(&client->token[i * 2], 3, "%02x", raw[i]);
    }
    strlcpy(client->name, (name != NULL && name[0] != '\0') ? name : "client",
            sizeof(client->name));
    s_count++;
    persist();
    xSemaphoreGive(s_lock);

    strcpy(out_token, client->token);
    ESP_LOGI(TAG, "paired \"%s\" (%u/%u clients)", client->name, (unsigned)s_count,
             AUTH_MAX_CLIENTS);
    return ESP_OK;
}

// Compare the full length regardless of where the mismatch is, so response
// timing doesn't leak how much of a guessed token was right.
static bool token_eq(const char *a, const char *b)
{
    volatile unsigned char diff = 0;
    for (size_t i = 0; i < AUTH_TOKEN_LEN; i++) {
        diff |= (unsigned char)a[i] ^ (unsigned char)b[i];
    }
    return diff == 0;
}

static int find_token(const char *token)
{
    if (strlen(token) != AUTH_TOKEN_LEN) {
        return -1;
    }
    for (size_t i = 0; i < s_count; i++) {
        if (token_eq(token, s_clients[i].token)) {
            return (int)i;
        }
    }
    return -1;
}

// Extract the bearer token from `req` into `out`; false if absent/malformed.
static bool bearer_token(httpd_req_t *req, char out[AUTH_TOKEN_LEN + 1])
{
    char header[96];
    if (httpd_req_get_hdr_value_str(req, "Authorization", header, sizeof(header)) !=
        ESP_OK) {
        return false;
    }
    const char *prefix = "Bearer ";
    if (strncasecmp(header, prefix, strlen(prefix)) != 0) {
        return false;
    }
    strlcpy(out, header + strlen(prefix), AUTH_TOKEN_LEN + 1);
    return strlen(out) == AUTH_TOKEN_LEN;
}

bool auth_check(httpd_req_t *req)
{
    char token[AUTH_TOKEN_LEN + 1];
    if (!bearer_token(req, token)) {
        return false;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool ok = find_token(token) >= 0;
    xSemaphoreGive(s_lock);
    return ok;
}

esp_err_t auth_revoke(const char *token)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int idx = find_token(token);
    if (idx < 0) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGI(TAG, "revoked \"%s\"", s_clients[idx].name);
    s_clients[idx] = s_clients[s_count - 1];
    s_count--;
    persist();
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

size_t auth_list_names(char out[][AUTH_NAME_MAX + 1], size_t max)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    size_t n = s_count < max ? s_count : max;
    for (size_t i = 0; i < n; i++) {
        strcpy(out[i], s_clients[i].name);
    }
    xSemaphoreGive(s_lock);
    return n;
}

size_t auth_client_count(void)
{
    return s_count;
}

void auth_clear_all(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_count = 0;
    memset(s_clients, 0, sizeof(s_clients));
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_erase_all(nvs);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
    xSemaphoreGive(s_lock);
    ESP_LOGW(TAG, "all paired clients forgotten");
}
