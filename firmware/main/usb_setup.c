#include "firmware_update.h"
#include "display.h"
#include "usb_setup.h"

#include <string.h>

#include "cJSON.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_ota_ops.h" // ESP_ERR_OTA_VALIDATE_FAILED
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "tinyusb_cdc_acm.h"
#include "tusb.h" // tud_cdc_n_write_clear / read_flush

#include "app_version.h"
#include "usb_mode.h"
#include "auth.h"
#include "cloud.h"
#include "operation.h"
#include "link_protocol.h"
#include "device_name.h"
#include "led.h"
#include "ota.h"
#include "wifi_mgr.h"

static const char *TAG = "usb_setup";

#define CDC_ITF TINYUSB_CDC_ACM_0
#define SETUP_LINE_MAX 1024   // largest command: provision (ssid+password+name)
#define UPDATE_CHUNK 2048
#define UPDATE_IDLE_TIMEOUT_MS 10000 // mid-stream silence = host went away
#define PROGRESS_EVERY_BYTES (64 * 1024)
/// wifi_mgr resolves a trial within ~30 s on its own; this is the backstop.
#define PROVISION_WAIT_MS 40000

static SemaphoreHandle_t s_rx_signal; // "the RX FIFO has data" latch
static volatile bool s_new_session;   // DTR rose: a fresh host connection

static SemaphoreHandle_t s_prov_done;
static wifi_provision_result_t s_prov_result;
static char s_prov_ip[16];

/* --- transport ------------------------------------------------------------- */

static void on_cdc_rx(int itf, cdcacm_event_t *event)
{
    xSemaphoreGive(s_rx_signal);
}

// Ember Bridge opens the port fresh for every command. Whatever a previous
// session left behind — an undelivered response in the TX FIFO, unread
// request bytes, a half-accumulated line in the worker — must not leak
// into the new one.
static void on_cdc_line_state(int itf, cdcacm_event_t *event)
{
    if (event->line_state_changed_data.dtr) {
        tud_cdc_n_write_clear(CDC_ITF);
        tud_cdc_n_read_flush(CDC_ITF);
        s_new_session = true;
        xSemaphoreGive(s_rx_signal); // wake the worker so it resets promptly
    }
}

#define READ_WAIT_FOREVER UINT32_MAX

/// Read up to `want` bytes, blocking until at least one arrives or the line
/// has been idle for `idle_timeout_ms`. Backpressure is free: bytes we leave
/// in the FIFO make TinyUSB NAK the host, so nothing is ever dropped.
static size_t read_bytes(uint8_t *dst, size_t want, uint32_t idle_timeout_ms)
{
    TickType_t wait = (idle_timeout_ms == READ_WAIT_FOREVER) ? portMAX_DELAY
                                                             : pdMS_TO_TICKS(idle_timeout_ms);
    size_t got = 0;
    while (got < want) {
        size_t n = 0;
        if (tinyusb_cdcacm_read(CDC_ITF, dst + got, want - got, &n) != ESP_OK) {
            n = 0;
        }
        if (n == 0) {
            if (got > 0) {
                break; // deliver what we have; caller loops for more
            }
            if (xSemaphoreTake(s_rx_signal, wait) != pdTRUE) {
                break;
            }
            continue;
        }
        got += n;
    }
    return got;
}

// A line must reach the host whole or not at all. Responses can exceed the
// TX FIFO (a busy-neighborhood scan is >1 KiB vs a 1 KiB FIFO), so queue in
// chunks as the host drains it; if the host stops reading (closed the port
// mid-exchange), CLEAR the FIFO rather than strand a torn JSON prefix —
// the next reader would find it glued to the front of its own response and
// choke, poisoning every exchange after it.
#define SEND_LINE_DEADLINE_MS 2000

static void send_line(cJSON *body)
{
    char *text = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (text == NULL) {
        return;
    }
    size_t len = strlen(text);
    text[len] = '\n'; // overwrite the terminator; we track len ourselves

    size_t sent = 0;
    TickType_t give_up = xTaskGetTickCount() + pdMS_TO_TICKS(SEND_LINE_DEADLINE_MS);
    while (sent < len + 1) {
        sent += tinyusb_cdcacm_write_queue(CDC_ITF, (const uint8_t *)text + sent,
                                           len + 1 - sent);
        tinyusb_cdcacm_write_flush(CDC_ITF, pdMS_TO_TICKS(100));
        if (sent < len + 1 && xTaskGetTickCount() >= give_up) {
            tud_cdc_n_write_clear(CDC_ITF); // no reader: deliver none of it
            break;
        }
    }
    free(text);
}

/// Response skeleton echoing the request's "id" (number or string), if any.
static cJSON *response_for(const cJSON *request, bool ok)
{
    cJSON *body = cJSON_CreateObject();
    const cJSON *id = cJSON_GetObjectItem(request, "id");
    if (id != NULL) {
        cJSON_AddItemToObject(body, "id", cJSON_Duplicate(id, false));
    }
    cJSON_AddBoolToObject(body, "ok", ok);
    return body;
}

static void reply_error(const cJSON *request, const char *code, const char *message)
{
    cJSON *body = response_for(request, false);
    cJSON *error = cJSON_AddObjectToObject(body, "error");
    cJSON_AddStringToObject(error, "code", code);
    cJSON_AddStringToObject(error, "message", message);
    send_line(body);
}

/* --- commands -------------------------------------------------------------- */

static const char *serial(void)
{
    static char s_serial[13];
    if (s_serial[0] == '\0') {
        uint8_t mac[6];
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        snprintf(s_serial, sizeof(s_serial), "%02X%02X%02X%02X%02X%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }
    return s_serial;
}

static void reboot_later(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();
}

static void add_display_settings(cJSON *body)
{
    display_settings_t settings = display_get_settings();
    cJSON *display = cJSON_AddObjectToObject(body, "display");
    cJSON_AddBoolToObject(display, "enabled", settings.enabled);
    cJSON_AddNumberToObject(display, "rotation", settings.rotation);
    cJSON_AddBoolToObject(display, "ledEnabled", settings.led_enabled);
}

static void cmd_set_display(const cJSON *request)
{
    const cJSON *enabled = cJSON_GetObjectItemCaseSensitive(request, "enabled");
    const cJSON *rotation = cJSON_GetObjectItemCaseSensitive(request, "rotation");
    const cJSON *led = cJSON_GetObjectItemCaseSensitive(request, "ledEnabled");
    if ((led && !cJSON_IsBool(led)) || !cJSON_IsBool(enabled) || !cJSON_IsNumber(rotation) ||
        (rotation->valuedouble != 0 && rotation->valuedouble != 180)) {
        reply_error(request, "invalid_display", "enabled/ledEnabled must be boolean and rotation must be 0 or 180");
        return;
    }
    if (!operation_begin()) { reply_error(request, "busy", "a transfer or update is running"); return; }
    display_settings_t settings = display_get_settings();
    settings.enabled = cJSON_IsTrue(enabled);
    settings.rotation = (uint16_t)rotation->valuedouble;
    if (led) settings.led_enabled = cJSON_IsTrue(led); // Older clients preserve the LED preference.
    esp_err_t err = display_configure(settings);
    operation_end();
    if (err != ESP_OK) { reply_error(request, "storage_error", "could not save display settings"); return; }
    cJSON *body = response_for(request, true);
    add_display_settings(body);
    send_line(body);
}

static void cmd_info(const cJSON *request)
{
    char ip[16], ssid[33], last_error[96], name[DEVICE_NAME_MAX + 1];
    wifi_mgr_get_ip(ip);
    wifi_mgr_configured_ssid(ssid);
    wifi_mgr_last_error(last_error);
    device_name_get(name);

    ota_status_t st;
    ota_get_status(&st);

    cJSON *body = response_for(request, true);
    add_display_settings(body);
    cJSON_AddStringToObject(body, "name", EMBER_LINK_NAME);
    cJSON_AddStringToObject(body, "deviceName", name);
    cJSON_AddStringToObject(body, "version", EMBER_LINK_VERSION);
    cJSON_AddStringToObject(body, "usbMode", usb_mode_name());
    cJSON_AddStringToObject(body, "serial", serial());
    cJSON_AddNumberToObject(body, "usbProtocolVersion", 1);
    cJSON_AddNumberToObject(body, "cloudProtocolVersion", 1);
    cJSON_AddNumberToObject(body, "setupProtocolVersion", 1);
    cJSON_AddItemToObject(body, "cloud", cloud_status());
    cJSON_AddBoolToObject(body, "provisioned", wifi_mgr_has_credentials());

    cJSON *wifi = cJSON_AddObjectToObject(body, "wifi");
    cJSON_AddBoolToObject(wifi, "setupMode", wifi_mgr_in_setup());
    cJSON_AddBoolToObject(wifi, "connected", strcmp(ip, "0.0.0.0") != 0);
    cJSON_AddStringToObject(wifi, "ip", ip);
    cJSON_AddStringToObject(wifi, "configuredSsid", ssid);
    cJSON_AddStringToObject(wifi, "lastError", last_error);

    cJSON *update = cJSON_AddObjectToObject(body, "update");
    cJSON_AddStringToObject(update, "slot", st.slot);
    cJSON_AddBoolToObject(update, "pendingVerify", st.pending_verify);
    cJSON_AddNumberToObject(update, "maxImageSize", (double)st.max_image_size);
    firmware_update_add_capabilities(body);
    send_line(body);
}

static void cmd_scan(const cJSON *request)
{
    static wifi_scan_result_t results[24];
    size_t n = wifi_mgr_scan(results, 24);

    cJSON *body = response_for(request, true);
    cJSON *list = cJSON_AddArrayToObject(body, "networks");
    for (size_t i = 0; i < n; i++) {
        cJSON *net = cJSON_CreateObject();
        cJSON_AddStringToObject(net, "ssid", results[i].ssid);
        cJSON_AddNumberToObject(net, "rssi", results[i].rssi);
        cJSON_AddBoolToObject(net, "secure", results[i].secure);
        cJSON_AddItemToArray(list, net);
    }
    send_line(body);
}

static void provision_cb(wifi_provision_result_t result, const char *ip)
{
    s_prov_result = result;
    strlcpy(s_prov_ip, ip ? ip : "", sizeof(s_prov_ip));
    xSemaphoreGive(s_prov_done);
}

static void cmd_provision(const cJSON *request)
{
    const cJSON *ssid = cJSON_GetObjectItem(request, "ssid");
    const cJSON *password = cJSON_GetObjectItem(request, "password");
    const cJSON *name = cJSON_GetObjectItem(request, "name");
    if (!cJSON_IsString(ssid) || ssid->valuestring[0] == '\0') {
        reply_error(request, "ssid_required", "ssid is required");
        return;
    }
    if (cJSON_IsString(name) && device_name_set(name->valuestring) != ESP_OK) {
        reply_error(request, "invalid_name", "name is too long");
        return;
    }

    bool was_setup = wifi_mgr_in_setup();
    led_set(LED_CONNECTING);
    xSemaphoreTake(s_prov_done, 0); // clear a stale result, defensively

    esp_err_t err = wifi_mgr_provision(
        ssid->valuestring, cJSON_IsString(password) ? password->valuestring : "",
        provision_cb);
    if (err == ESP_ERR_INVALID_ARG) {
        reply_error(request, "invalid_credentials", "ssid or password too long");
        return;
    }
    if (err != ESP_OK) {
        reply_error(request, "busy", "another provisioning attempt is running");
        return;
    }

    if (xSemaphoreTake(s_prov_done, pdMS_TO_TICKS(PROVISION_WAIT_MS)) != pdTRUE) {
        s_prov_result = WIFI_PROVISION_TIMEOUT; // backstop; should not happen
    }

    if (s_prov_result == WIFI_PROVISION_OK) {
        // LED already went green via the CONNECTED state callback.
        cJSON *body = response_for(request, true);
        cJSON_AddStringToObject(body, "ssid", ssid->valuestring);
        cJSON_AddStringToObject(body, "ip", s_prov_ip);
        send_line(body);
        ESP_LOGI(TAG, "provisioned over USB onto \"%s\"", ssid->valuestring);
        return;
    }

    // Failed trials revert; reflect the reverted state on the LED. (A dongle
    // that hopped back to its old network gets CONNECTING/CONNECTED events,
    // which repaint the LED on their own.)
    if (was_setup) {
        led_set(LED_SETUP);
    }
    switch (s_prov_result) {
    case WIFI_PROVISION_AUTH_FAILED:
        reply_error(request, "wrong_password", "the network rejected the password");
        break;
    case WIFI_PROVISION_NOT_FOUND:
        reply_error(request, "network_not_found",
                    "no such network in range (5 GHz-only networks are invisible "
                    "to the dongle)");
        break;
    default:
        reply_error(request, "join_timeout", "the network never handed out an address");
        break;
    }
}

static void cmd_set_name(const cJSON *request)
{
    const cJSON *name = cJSON_GetObjectItem(request, "name");
    if (!cJSON_IsString(name)) {
        reply_error(request, "name_required", "name is required");
        return;
    }
    if (device_name_set(name->valuestring) != ESP_OK) {
        reply_error(request, "invalid_name", "name is too long");
        return;
    }
    send_line(response_for(request, true));
}

static void cmd_pair(const cJSON *request)
{
    const cJSON *name = cJSON_GetObjectItem(request, "name");

    // Being on the wired end of the cable is a stronger consent gesture than
    // the button tap, so pairing over USB is never "closed".
    auth_open_window();

    char token[AUTH_TOKEN_LEN + 1];
    esp_err_t err = auth_pair(cJSON_IsString(name) ? name->valuestring : "", token);
    if (err == ESP_ERR_NO_MEM) {
        reply_error(request, "too_many_clients",
                    "client limit reached — factory-reset the dongle to clear it");
        return;
    }
    if (err != ESP_OK) {
        reply_error(request, "pair_failed", "could not store the new client");
        return;
    }
    cJSON *body = response_for(request, true);
    cJSON_AddStringToObject(body, "token", token);
    cJSON_AddStringToObject(body, "serial", serial());
    send_line(body);
}

static void cmd_update(const cJSON *request)
{
    const cJSON *size = cJSON_GetObjectItem(request, "size");
    if (!cJSON_IsNumber(size) || !link_integer_valid(size->valuedouble, 1, 3 * 1024 * 1024)) {
        reply_error(request, "size_required", "size must be the image byte count");
        return;
    }
    size_t total = (size_t)size->valuedouble;

    esp_err_t err = ota_begin(total);
    if (err == ESP_ERR_INVALID_STATE) {
        reply_error(request, "update_in_progress", "another update is already running");
        return;
    }
    if (err == ESP_ERR_INVALID_SIZE) {
        reply_error(request, "image_too_large", "image exceeds the OTA partition");
        return;
    }
    if (err != ESP_OK) {
        reply_error(request, "update_failed", "could not start the update");
        return;
    }

    led_set(LED_UPDATE);
    cJSON *ready = response_for(request, true);
    cJSON_AddBoolToObject(ready, "ready", true);
    send_line(ready); // the host must wait for this before streaming bytes

    static uint8_t chunk[UPDATE_CHUNK];
    size_t written = 0, last_progress = 0;
    while (written < total) {
        size_t want = total - written;
        if (want > sizeof(chunk)) {
            want = sizeof(chunk);
        }
        size_t got = read_bytes(chunk, want, UPDATE_IDLE_TIMEOUT_MS);
        if (got == 0) {
            ota_abort();
            led_set(LED_ERROR);
            reply_error(request, "upload_failed", "transfer stalled");
            return;
        }
        err = ota_write(chunk, got);
        if (err != ESP_OK) {
            ota_abort();
            led_set(LED_ERROR);
            if (err == ESP_ERR_INVALID_ARG) {
                reply_error(request, "wrong_image",
                            "not an Ember Link firmware image for this device");
            } else {
                reply_error(request, "update_failed", "flash write failed");
            }
            return;
        }
        written += got;

        if (written - last_progress >= PROGRESS_EVERY_BYTES || written == total) {
            last_progress = written;
            cJSON *event = cJSON_CreateObject();
            cJSON_AddStringToObject(event, "event", "update");
            cJSON_AddNumberToObject(event, "written", (double)written);
            cJSON_AddNumberToObject(event, "total", (double)total);
            send_line(event);
        }
    }

    err = ota_finish();
    if (err != ESP_OK) {
        led_set(LED_ERROR);
        if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
            reply_error(request, "invalid_signature",
                        "image is corrupt or not signed with the Ember Link key");
        } else {
            reply_error(request, "update_failed", "could not finalize the update");
        }
        return;
    }

    ESP_LOGI(TAG, "update accepted over USB, rebooting");
    cJSON *body = response_for(request, true);
    cJSON_AddStringToObject(body, "message", "update verified; rebooting");
    send_line(body);
    xTaskCreate(reboot_later, "reboot", 2048, NULL, 5, NULL);
}

static void cmd_factory_reset(const cJSON *request)
{
    if (!operation_begin()) { reply_error(request, "busy", "a transfer or update is running"); return; }
    if (cloud_disable_for_reset() != ESP_OK) { operation_end(); reply_error(request, "busy", "cloud request in progress"); return; }
    if (display_configure(display_settings_defaults()) != ESP_OK) { operation_end(); reply_error(request, "storage_error", "could not reset display settings"); return; }
    ESP_LOGW(TAG, "factory reset over USB");
    wifi_mgr_clear_credentials();
    auth_clear_all();
    device_name_clear();
    cJSON *body = response_for(request, true);
    cJSON_AddStringToObject(body, "message", "wiped; rebooting into setup mode");
    send_line(body);
    xTaskCreate(reboot_later, "reboot", 2048, NULL, 5, NULL);
}

/* --- worker ---------------------------------------------------------------- */

static void handle_line(const char *line)
{
    cJSON *request = cJSON_Parse(line);
    if (request == NULL) {
        reply_error(NULL, "parse_error", "each request must be one JSON line");
        return;
    }
    const cJSON *cmd = cJSON_GetObjectItem(request, "cmd");
    if (!cJSON_IsString(cmd)) {
        reply_error(request, "cmd_required", "missing \"cmd\"");
    } else if (strcmp(cmd->valuestring, "info") == 0) {
        cmd_info(request);
    } else if (strcmp(cmd->valuestring, "cloud_status") == 0) {
        cJSON *body=response_for(request,true);
        cJSON_AddItemToObject(body,"cloud",cloud_status());
        send_line(body);
    } else if (strcmp(cmd->valuestring, "cloud_claim") == 0) {
        esp_err_t err = cloud_claim(request);
        if (err == ESP_OK) send_line(response_for(request, true));
        else reply_error(request, err == ESP_ERR_INVALID_ARG ? "invalid_setup" : "busy_or_unconfigured",
                         "setup requires a provisioned device with no unsettled transfer");
    } else if (strcmp(cmd->valuestring, "cloud_configure") == 0) {
        esp_err_t err=cloud_configure(request);
        if (err==ESP_OK) send_line(response_for(request,true));
        else reply_error(request,err==ESP_ERR_INVALID_ARG ? "invalid_config" : "busy_or_storage_error",
                         "requires HTTPS origin, download host, device ID and 64-character lowercase hex token; pending receipts must be resolved first");
    } else if (strcmp(cmd->valuestring, "cloud_enable") == 0) {
        const cJSON *enabled=cJSON_GetObjectItemCaseSensitive(request,"enabled");
        if (!cJSON_IsBool(enabled)) reply_error(request,"enabled_required","enabled must be a boolean");
        else if (cloud_set_enabled(cJSON_IsTrue(enabled))==ESP_OK) send_line(response_for(request,true));
        else reply_error(request,"busy_or_unconfigured","finish the current operation or configure cloud first");
    } else if (strcmp(cmd->valuestring, "scan") == 0) {
        cmd_scan(request);
    } else if (strcmp(cmd->valuestring, "provision") == 0) {
        if (!operation_begin()) reply_error(request,"busy","a transfer or update is running");
        else { cmd_provision(request); operation_end(); }
    } else if (strcmp(cmd->valuestring, "set_display") == 0) {
        cmd_set_display(request);
    } else if (strcmp(cmd->valuestring, "set_name") == 0) {
        if (!operation_begin()) reply_error(request,"busy","a transfer or update is running");
        else { cmd_set_name(request); operation_end(); }
    } else if (strcmp(cmd->valuestring, "pair") == 0) {
        cmd_pair(request);
    } else if (strcmp(cmd->valuestring, "update") == 0) {
        cmd_update(request);
    } else if (strcmp(cmd->valuestring, "factory_reset") == 0) {
        cmd_factory_reset(request);
    } else {
        reply_error(request, "unknown_command", cmd->valuestring);
    }
    cJSON_Delete(request);
}

static void worker_task(void *arg)
{
    static char line[SETUP_LINE_MAX];
    size_t len = 0;
    bool overflow = false;

    while (true) {
        uint8_t c;
        if (read_bytes(&c, 1, READ_WAIT_FOREVER) != 1) {
            continue;
        }
        // Checked after the read: `c` is the first byte OF the new session
        // (the DTR callback flushed everything older), so reset the line
        // accumulator but keep the byte.
        if (s_new_session) {
            s_new_session = false;
            len = 0;
            overflow = false;
        }
        if (c != '\n') {
            if (len < sizeof(line) - 1) {
                line[len++] = (char)c;
            } else {
                overflow = true; // swallow until the newline, then complain
            }
            continue;
        }
        line[len] = '\0';
        if (overflow) {
            reply_error(NULL, "line_too_long", "request exceeds 1023 bytes");
        } else if (len > 0 && line[0] != '\r') {
            if (line[len - 1] == '\r') {
                line[len - 1] = '\0';
            }
            handle_line(line);
        }
        len = 0;
        overflow = false;
    }
}

esp_err_t usb_setup_start(void)
{
    s_rx_signal = xSemaphoreCreateBinary();
    s_prov_done = xSemaphoreCreateBinary();
    if (s_rx_signal == NULL || s_prov_done == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const tinyusb_config_cdcacm_t cfg = {
        .cdc_port = CDC_ITF,
        .callback_rx = on_cdc_rx,
        .callback_line_state_changed = on_cdc_line_state,
    };
    esp_err_t err = tinyusb_cdcacm_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "cdc init failed: %s", esp_err_to_name(err));
        return err;
    }

    // Stack fits cJSON trees for scan results plus the WiFi scan machinery.
    if (xTaskCreate(worker_task, "usb_setup", 6144, NULL, 5, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "setup channel ready on CDC");
    return ESP_OK;
}
