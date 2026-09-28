#include "firmware_update.h"
#include "display.h"
#include "http_api.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_ota_ops.h" // ESP_ERR_OTA_VALIDATE_FAILED
#include "esp_system.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app_version.h"
#include "usb_mode.h"
#include "auth.h"
#include "operation.h"
#include "link_files.h"
#include "link_protocol.h"
#include "device_name.h"
#include "led.h"
#include "ota.h"
#include "storage.h"
#include "wifi_mgr.h"

static const char *TAG = "http";

#define UPLOAD_CHUNK 8192
#define MAX_UPLOAD_BYTES (64 * 1024 * 1024) // sanity cap; real limit is card space

/* --- helpers ------------------------------------------------------------- */

static char s_serial[13];

static const char *serial(void)
{
    if (s_serial[0] == '\0') {
        uint8_t mac[6];
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        snprintf(s_serial, sizeof(s_serial), "%02X%02X%02X%02X%02X%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }
    return s_serial;
}

static esp_err_t send_json(httpd_req_t *req, const char *status, cJSON *body)
{
    char *text = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (text == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "out of memory");
        return ESP_ERR_NO_MEM;
    }
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_send(req, text, HTTPD_RESP_USE_STRLEN);
    free(text);
    return err;
}

static esp_err_t send_error(httpd_req_t *req, const char *status, const char *code,
                            const char *message)
{
    cJSON *body = cJSON_CreateObject();
    cJSON *error = cJSON_AddObjectToObject(body, "error");
    cJSON_AddStringToObject(error, "code", code);
    cJSON_AddStringToObject(error, "message", message);
    return send_json(req, status, body);
}

// Auth gate for every endpoint except /api/health and /api/pair. Setup mode
// is exempt: the captive-portal page needs /api/wifi and /api/networks, and
// being on the dongle's own hotspot is already proof of presence.
#define REQUIRE_AUTH(req)                                                       \
    do {                                                                        \
        if (!wifi_mgr_in_setup() && !auth_check(req)) {                         \
            return send_error(req, "401 Unauthorized", "unauthorized",          \
                              "pair with the dongle first (POST /api/pair)");   \
        }                                                                       \
    } while (0)

// FAT long-filename rules, no path tricks: printable ASCII except the
// characters FAT itself forbids (\ / : * ? " < > |), must not start with a
// dot, needs an extension. Real design names contain spaces, parentheses,
// ampersands — "redWork (19).PES" must pass.
static bool filename_ok(const char *name)
{
    size_t len = strlen(name);
    if (len == 0 || len >= STORAGE_MAX_NAME || name[0] == '.' || name[0] == '~') {
        return false;
    }
    bool has_dot = false;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)name[i];
        if (c == '.') {
            has_dot = true;
            continue;
        }
        if (c < 0x20 || c > 0x7E || strchr("\\/:*?\"<>|", c) != NULL) {
            return false;
        }
    }
    return has_dot && name[len - 1] != '.' && name[len - 1] != ' ';
}

static void url_decode(char *s)
{
    char *out = s;
    while (*s) {
        if (*s == '%' && isxdigit((unsigned char)s[1]) && isxdigit((unsigned char)s[2])) {
            char hex[3] = {s[1], s[2], 0};
            *out++ = (char)strtol(hex, NULL, 16);
            s += 3;
        } else if (*s == '+') {
            *out++ = ' ';
            s++;
        } else {
            *out++ = *s++;
        }
    }
    *out = '\0';
}

/* --- GET /api/health, /api/info, /api/files ------------------------------ */

static esp_err_t health_get(httpd_req_t *req)
{
    char device_name[DEVICE_NAME_MAX + 1];
    device_name_get(device_name);

    cJSON *body = cJSON_CreateObject();
    cJSON_AddBoolToObject(body, "ok", true);
    cJSON_AddStringToObject(body, "name", EMBER_LINK_NAME);
    cJSON_AddStringToObject(body, "deviceName", device_name);
    cJSON_AddStringToObject(body, "version", EMBER_LINK_VERSION);
    cJSON_AddStringToObject(body, "usbMode", usb_mode_name());
    cJSON_AddStringToObject(body, "serial", serial());
    return send_json(req, "200 OK", body);
}

static esp_err_t info_get(httpd_req_t *req)
{
    REQUIRE_AUTH(req);
    uint64_t total = 0, free_bytes = 0;
    storage_cached_stats(&total, &free_bytes);

    char ip[16];
    wifi_mgr_get_ip(ip);

    char device_name[DEVICE_NAME_MAX + 1];
    device_name_get(device_name);

    cJSON *body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "name", EMBER_LINK_NAME);
    cJSON_AddStringToObject(body, "deviceName", device_name);
    cJSON_AddStringToObject(body, "version", EMBER_LINK_VERSION);
    cJSON_AddStringToObject(body, "usbMode", usb_mode_name());
    cJSON_AddStringToObject(body, "serial", serial());
    cJSON_AddStringToObject(body, "ip", ip);
    cJSON *storage = cJSON_AddObjectToObject(body, "storage");
    cJSON_AddNumberToObject(storage, "totalBytes", (double)total);
    cJSON_AddNumberToObject(storage, "freeBytes", (double)free_bytes);
    return send_json(req, "200 OK", body);
}

static esp_err_t files_get(httpd_req_t *req)
{
    REQUIRE_AUTH(req);
    static storage_file_t files[STORAGE_MAX_FILES];
    size_t n = storage_cached_files(files, STORAGE_MAX_FILES);

    cJSON *body = cJSON_CreateObject();
    cJSON *list = cJSON_AddArrayToObject(body, "files");
    for (size_t i = 0; i < n; i++) {
        cJSON *f = cJSON_CreateObject();
        cJSON_AddStringToObject(f, "name", files[i].name);
        cJSON_AddNumberToObject(f, "size", (double)files[i].size);
        cJSON_AddItemToArray(list, f);
    }
    return send_json(req, "200 OK", body);
}

/* --- POST /api/upload?filename=X ----------------------------------------- */

static esp_err_t upload_post(httpd_req_t *req)
{
    REQUIRE_AUTH(req);
    char query[256]={0}, name[STORAGE_MAX_NAME]={0};
    if (httpd_req_get_url_query_str(req,query,sizeof(query))!=ESP_OK ||
        httpd_query_key_value(query,"filename",name,sizeof(name))!=ESP_OK)
        return send_error(req,"400 Bad Request","filename_required","pass a filename");
    url_decode(name);
    if (!link_filename_valid(name)) return send_error(req,"400 Bad Request","invalid_filename","invalid root filename");
    if (!req->content_len || req->content_len>LINK_MAX_FILE_BYTES)
        return send_error(req,"400 Bad Request","invalid_size","invalid file size");
    if (!operation_begin()) return send_error(req,"409 Conflict","busy","another operation is running");
    esp_err_t err=storage_acquire();
    if (err!=ESP_OK) { operation_end(); return send_error(req,"500 Internal Server Error","storage_busy","cannot acquire storage"); }
    display_begin(false, false, name, req->content_len);
    led_set(LED_TRANSFER);
    link_file_write_t writer={0};
    err=link_file_begin(&writer,name,req->content_len);
    static uint8_t chunk[UPLOAD_CHUNK];
    size_t remaining=req->content_len;
    while (err==ESP_OK && remaining) {
        int n=httpd_req_recv(req,(char *)chunk,remaining<sizeof(chunk) ? remaining : sizeof(chunk));
        if (n<=0) { err=ESP_FAIL; break; }
        err=link_file_write(&writer,chunk,(size_t)n); remaining-=(size_t)n;
        if (err==ESP_OK) display_progress(req->content_len-remaining);
    }
    if (err==ESP_OK) err=link_file_finish(&writer,NULL);
    else link_file_abort(&writer);
    esp_err_t release=storage_release();
    if (err==ESP_OK) err=release;
    display_finish(err==ESP_OK, err==ESP_ERR_NO_MEM ? "Card full" : "Transfer failed");
    operation_end();
    led_set(err==ESP_OK ? LED_READY : LED_ERROR);
    if (err!=ESP_OK) return send_error(req,err==ESP_ERR_NO_MEM ? "507 Insufficient Storage" : "500 Internal Server Error",
        err==ESP_ERR_NO_MEM ? "insufficient_storage" : "upload_failed","could not store the design");
    cJSON *body=cJSON_CreateObject(); cJSON_AddBoolToObject(body,"ok",true);
    cJSON *file=cJSON_AddObjectToObject(body,"file");
    cJSON_AddStringToObject(file,"name",name); cJSON_AddNumberToObject(file,"size",req->content_len);
    return send_json(req,"201 Created",body);
}

/* --- DELETE /api/files/X -------------------------------------------------- */

static esp_err_t file_delete(httpd_req_t *req)
{
    REQUIRE_AUTH(req);
    const char *prefix = "/api/files/";
    char name[STORAGE_MAX_NAME];
    strlcpy(name, req->uri + strlen(prefix), sizeof(name));
    url_decode(name);
    if (!filename_ok(name)) {
        return send_error(req, "400 Bad Request", "invalid_filename", "bad file name");
    }

    if (!operation_begin()) return send_error(req,"409 Conflict","busy","another operation is running");
    if (storage_acquire() != ESP_OK) {
        operation_end();
        return send_error(req, "500 Internal Server Error", "storage_busy",
                          "could not take the SD card from the USB host");
    }
    char path[STORAGE_MAX_NAME + 16];
    snprintf(path, sizeof(path), "%s/%s", storage_base_path(), name);
    int rc = remove(path);
    storage_release();
    operation_end();

    if (rc != 0) {
        return send_error(req, "404 Not Found", "file_not_found", "no such file");
    }
    cJSON *body = cJSON_CreateObject();
    cJSON_AddBoolToObject(body, "ok", true);
    return send_json(req, "200 OK", body);
}

/* --- provisioning --------------------------------------------------------- */

/// GET /api/networks — scan for nearby WiFi (blocking ~2 s).
static esp_err_t networks_get(httpd_req_t *req)
{
    REQUIRE_AUTH(req);
    static wifi_scan_result_t results[24];
    size_t n = wifi_mgr_scan(results, 24);

    cJSON *body = cJSON_CreateObject();
    cJSON *list = cJSON_AddArrayToObject(body, "networks");
    for (size_t i = 0; i < n; i++) {
        cJSON *net = cJSON_CreateObject();
        cJSON_AddStringToObject(net, "ssid", results[i].ssid);
        cJSON_AddNumberToObject(net, "rssi", results[i].rssi);
        cJSON_AddBoolToObject(net, "secure", results[i].secure);
        cJSON_AddItemToArray(list, net);
    }
    return send_json(req, "200 OK", body);
}

/// GET /api/wifi — provisioning state: what is configured, why the last
/// join failed. The setup page uses this to explain itself.
static esp_err_t wifi_get(httpd_req_t *req)
{
    REQUIRE_AUTH(req);
    char ssid[33], last_error[96];
    wifi_mgr_configured_ssid(ssid);
    wifi_mgr_last_error(last_error);

    cJSON *body = cJSON_CreateObject();
    cJSON_AddBoolToObject(body, "setupMode", wifi_mgr_in_setup());
    cJSON_AddStringToObject(body, "configuredSsid", ssid);
    cJSON_AddStringToObject(body, "lastError", last_error);
    return send_json(req, "200 OK", body);
}

/// Captive-portal catch-all: in setup mode, every unknown GET (that is,
/// every OS connectivity check — generate_204, hotspot-detect.html, ...)
/// redirects to the setup page, which makes phones pop the sign-in sheet.
static esp_err_t catchall_get(httpd_req_t *req)
{
    if (!wifi_mgr_in_setup()) {
        return send_error(req, "404 Not Found", "not_found", "no such path");
    }
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    return httpd_resp_send(req, NULL, 0);
}

static void reboot_later(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();
}

static esp_err_t wifi_post(httpd_req_t *req)
{
    REQUIRE_AUTH(req);
    char buf[256];
    int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len <= 0) {
        return send_error(req, "400 Bad Request", "body_required", "expected JSON body");
    }
    buf[len] = '\0';

    cJSON *json = cJSON_Parse(buf);
    const cJSON *ssid = cJSON_GetObjectItem(json, "ssid");
    const cJSON *password = cJSON_GetObjectItem(json, "password");
    if (!cJSON_IsString(ssid) || ssid->valuestring[0] == '\0') {
        cJSON_Delete(json);
        return send_error(req, "400 Bad Request", "ssid_required", "ssid is required");
    }
    if (!operation_begin()) { cJSON_Delete(json); return send_error(req,"409 Conflict","busy","another operation is running"); }
    esp_err_t err = wifi_mgr_save_credentials(
        ssid->valuestring, cJSON_IsString(password) ? password->valuestring : "");
    cJSON_Delete(json);
    if (err != ESP_OK) {
        operation_end();
        return send_error(req, "500 Internal Server Error", "save_failed",
                          "could not persist credentials");
    }

    ESP_LOGI(TAG, "credentials saved; rebooting to join the network");
    xTaskCreate(reboot_later, "reboot", 2048, NULL, 5, NULL);
    cJSON *body = cJSON_CreateObject();
    cJSON_AddBoolToObject(body, "ok", true);
    cJSON_AddStringToObject(body, "message", "rebooting to join your WiFi");
    return send_json(req, "200 OK", body);
}

/* --- pairing ---------------------------------------------------------------- */

/// POST /api/pair {"name": "Matt's MacBook"} — mint a token for a new
/// client. Only allowed while the pairing window is open (power-on, button
/// tap, or setup mode); refused with pairing_closed otherwise.
static esp_err_t pair_post(httpd_req_t *req)
{
    char name[AUTH_NAME_MAX + 1] = "";
    char buf[192];
    int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len > 0) {
        buf[len] = '\0';
        cJSON *json = cJSON_Parse(buf);
        const cJSON *n = cJSON_GetObjectItem(json, "name");
        if (cJSON_IsString(n)) {
            strlcpy(name, n->valuestring, sizeof(name));
        }
        cJSON_Delete(json);
    }

    char token[AUTH_TOKEN_LEN + 1];
    esp_err_t err = auth_pair(name, token);
    if (err == ESP_ERR_INVALID_STATE) {
        return send_error(req, "403 Forbidden", "pairing_closed",
                          "unplug and replug the dongle (or tap its button), "
                          "then pair within 5 minutes");
    }
    if (err == ESP_ERR_NO_MEM) {
        return send_error(req, "403 Forbidden", "too_many_clients",
                          "client limit reached — hold the button 5 s to "
                          "factory-reset the dongle");
    }
    if (err != ESP_OK) {
        return send_error(req, "500 Internal Server Error", "pair_failed",
                          "could not store the new client");
    }

    cJSON *body = cJSON_CreateObject();
    cJSON_AddBoolToObject(body, "ok", true);
    cJSON_AddStringToObject(body, "token", token);
    cJSON_AddStringToObject(body, "serial", serial());
    return send_json(req, "201 Created", body);
}

/// GET /api/pair — names of paired clients (never their tokens).
static esp_err_t pair_get(httpd_req_t *req)
{
    REQUIRE_AUTH(req);
    static char names[AUTH_MAX_CLIENTS][AUTH_NAME_MAX + 1];
    size_t n = auth_list_names(names, AUTH_MAX_CLIENTS);

    cJSON *body = cJSON_CreateObject();
    cJSON_AddBoolToObject(body, "pairingOpen", auth_pairing_open());
    cJSON *list = cJSON_AddArrayToObject(body, "clients");
    for (size_t i = 0; i < n; i++) {
        cJSON_AddItemToArray(list, cJSON_CreateString(names[i]));
    }
    return send_json(req, "200 OK", body);
}

/// DELETE /api/pair — a client revoking its own token.
static esp_err_t pair_delete(httpd_req_t *req)
{
    REQUIRE_AUTH(req);
    // REQUIRE_AUTH can pass headerless in setup mode; the revoke target is
    // always the presented token, so demand one explicitly.
    char header[96] = {0};
    if (httpd_req_get_hdr_value_str(req, "Authorization", header, sizeof(header)) !=
            ESP_OK ||
        strncasecmp(header, "Bearer ", 7) != 0) {
        return send_error(req, "400 Bad Request", "token_required",
                          "send the token to revoke as a Bearer header");
    }
    if (auth_revoke(header + 7) != ESP_OK) {
        return send_error(req, "404 Not Found", "unknown_token", "no such client");
    }
    cJSON *body = cJSON_CreateObject();
    cJSON_AddBoolToObject(body, "ok", true);
    return send_json(req, "200 OK", body);
}

/* --- firmware updates ------------------------------------------------------ */

/// GET /api/update — running version, boot slot, whether we're in the
/// post-update probation window, and the largest image the dongle accepts.
static esp_err_t update_get(httpd_req_t *req)
{
    REQUIRE_AUTH(req);
    ota_status_t st;
    ota_get_status(&st);

    cJSON *body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "version", st.version);
    cJSON_AddStringToObject(body, "slot", st.slot);
    cJSON_AddBoolToObject(body, "pendingVerify", st.pending_verify);
    cJSON_AddNumberToObject(body, "maxImageSize", (double)st.max_image_size);
    cJSON_AddStringToObject(body, "serial", serial());
    firmware_update_add_capabilities(body);
    return send_json(req, "200 OK", body);
}

/// POST /api/update — body is a signed firmware image. Written to the
/// inactive OTA slot; only booted if the RSA signature verifies against the
/// key in the running firmware. A bad flash rolls back on next power cycle.
static esp_err_t update_post(httpd_req_t *req)
{
    REQUIRE_AUTH(req);
    if (req->content_len == 0) {
        return send_error(req, "400 Bad Request", "body_required",
                          "body must be the signed firmware image");
    }

    esp_err_t err = ota_begin(req->content_len);
    if (err == ESP_ERR_INVALID_STATE) {
        return send_error(req, "409 Conflict", "update_in_progress",
                          "another update is already running");
    }
    if (err == ESP_ERR_INVALID_SIZE) {
        return send_error(req, "400 Bad Request", "image_too_large",
                          "image exceeds the OTA partition");
    }
    if (err != ESP_OK) {
        return send_error(req, "500 Internal Server Error", "update_failed",
                          "could not start the update");
    }
    led_set(LED_UPDATE);

    static char chunk[UPLOAD_CHUNK];
    size_t remaining = req->content_len;
    while (remaining > 0) {
        int got = httpd_req_recv(req, chunk,
                                 remaining < UPLOAD_CHUNK ? remaining : UPLOAD_CHUNK);
        if (got <= 0) {
            ota_abort();
            led_set(LED_ERROR);
            return send_error(req, "500 Internal Server Error", "upload_failed",
                              "transfer interrupted");
        }
        err = ota_write(chunk, got);
        if (err != ESP_OK) {
            ota_abort();
            led_set(LED_ERROR);
            if (err == ESP_ERR_INVALID_ARG) {
                return send_error(req, "400 Bad Request", "wrong_image",
                                  "not an Ember Link firmware image for this device");
            }
            return send_error(req, "500 Internal Server Error", "update_failed",
                              "flash write failed");
        }
        remaining -= got;
    }

    err = ota_finish();
    if (err != ESP_OK) {
        led_set(LED_ERROR);
        if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
            return send_error(req, "400 Bad Request", "invalid_signature",
                              "image is corrupt or not signed with the Ember Link key");
        }
        return send_error(req, "500 Internal Server Error", "update_failed",
                          "could not finalize the update");
    }

    ESP_LOGI(TAG, "update accepted, rebooting");
    xTaskCreate(reboot_later, "reboot", 2048, NULL, 5, NULL);
    cJSON *body = cJSON_CreateObject();
    cJSON_AddBoolToObject(body, "ok", true);
    cJSON_AddStringToObject(body, "message", "update verified; rebooting");
    return send_json(req, "200 OK", body);
}

static const char SETUP_PAGE[] =
    "<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>Ember Link setup</title>"
    "<style>body{font-family:system-ui;max-width:22rem;margin:2rem auto;padding:0 1rem}"
    "input,button{width:100%;padding:.6rem;margin:.3rem 0;font-size:1rem;box-sizing:border-box}"
    "button{background:#e25822;color:#fff;border:0;border-radius:.3rem}"
    "#err{background:#fee;border:1px solid #c00;color:#900;padding:.5rem;border-radius:.3rem}"
    "#list div{padding:.5rem;border:1px solid #ddd;border-radius:.3rem;margin:.25rem 0;cursor:pointer}"
    "#list div.sel{border-color:#e25822;background:#fff3ee}"
    ".bars{float:right;color:#888;font-size:.85rem}</style>"
    "<h2>Ember Link</h2>"
    "<p id=err hidden></p>"
    "<p>Choose your home WiFi. The light turns <b style='color:green'>green</b> "
    "once the dongle is connected.</p>"
    "<div id=list>Scanning for networks…</div>"
    "<input id=s placeholder='Network name (or pick above)'>"
    "<input id=p type=password placeholder='WiFi password'>"
    "<button onclick='save()'>Save &amp; connect</button>"
    "<p id=m></p>"
    "<script>"
    "const $=id=>document.getElementById(id);"
    "function bars(r){return r>-55?'\\u25AE\\u25AE\\u25AE':r>-70?'\\u25AE\\u25AE\\u25AF':'\\u25AE\\u25AF\\u25AF'}"
    "async function load(){"
    "try{const w=await(await fetch('/api/wifi')).json();"
    "if(w.lastError){$('err').textContent=w.lastError;$('err').hidden=false}"
    "if(w.configuredSsid)$('s').value=w.configuredSsid;}catch(e){}"
    "try{const j=await(await fetch('/api/networks')).json();"
    "if(!j.networks.length){$('list').textContent='No networks found — type the name below.';return}"
    "$('list').textContent='';"
    "for(const n of j.networks){const d=document.createElement('div');"
    "d.innerHTML=(n.secure?'\\uD83D\\uDD12 ':'')+n.ssid+'<span class=bars>'+bars(n.rssi)+'</span>';"
    "d.onclick=()=>{$('s').value=n.ssid;"
    "for(const o of $('list').children)o.classList.remove('sel');d.classList.add('sel');$('p').focus()};"
    "$('list').appendChild(d)}}"
    "catch(e){$('list').textContent='Scan failed — type the network name below.'}}"
    "async function save(){$('m').textContent='Saving\\u2026';"
    "const r=await fetch('/api/wifi',{method:'POST',body:JSON.stringify("
    "{ssid:$('s').value,password:$('p').value})});"
    "const j=await r.json();"
    "$('m').textContent=j.ok?'Saved! The dongle is rebooting and will join your WiFi.':"
    "(j.error?j.error.message:'failed');}"
    "load();"
    "</script>";

static esp_err_t root_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, SETUP_PAGE, HTTPD_RESP_USE_STRLEN);
}

/* --- server -------------------------------------------------------------- */

esp_err_t http_api_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.stack_size = 8192; // upload handler owns file + JSON buffers
    config.lru_purge_enable = true;
    config.max_uri_handlers = 24; // default 8 is fewer than our routes

    httpd_handle_t server = NULL;
    esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK) {
        return err;
    }

    const httpd_uri_t routes[] = {
        // Registration order matters with wildcard matching: the GET
        // catch-all (captive portal) must come last.
        {.uri = "/", .method = HTTP_GET, .handler = root_get},
        {.uri = "/api/health", .method = HTTP_GET, .handler = health_get},
        {.uri = "/api/info", .method = HTTP_GET, .handler = info_get},
        {.uri = "/api/files", .method = HTTP_GET, .handler = files_get},
        {.uri = "/api/upload", .method = HTTP_POST, .handler = upload_post},
        {.uri = "/api/files/*", .method = HTTP_DELETE, .handler = file_delete},
        {.uri = "/api/wifi", .method = HTTP_GET, .handler = wifi_get},
        {.uri = "/api/wifi", .method = HTTP_POST, .handler = wifi_post},
        {.uri = "/api/networks", .method = HTTP_GET, .handler = networks_get},
        {.uri = "/api/update", .method = HTTP_GET, .handler = update_get},
        {.uri = "/api/update", .method = HTTP_POST, .handler = update_post},
        {.uri = "/api/pair", .method = HTTP_GET, .handler = pair_get},
        {.uri = "/api/pair", .method = HTTP_POST, .handler = pair_post},
        {.uri = "/api/pair", .method = HTTP_DELETE, .handler = pair_delete},
        {.uri = "/*", .method = HTTP_GET, .handler = catchall_get},
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        ESP_ERROR_CHECK(httpd_register_uri_handler(server, &routes[i]));
    }
    ESP_LOGI(TAG, "listening on :%d", config.server_port);
    return ESP_OK;
}
