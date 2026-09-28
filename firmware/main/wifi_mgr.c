#include "wifi_mgr.h"

#include <string.h>

#include "dhcpserver/dhcpserver.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"

static const char *TAG = "wifi";

#define NVS_NAMESPACE "emberconn"
#define RETRY_DELAY_MS 5000
/// Consecutive auth failures before concluding the password is wrong.
#define AUTH_FAIL_LIMIT 4
#define SCAN_MAX_RECORDS 24

/// Provisioning trials are interactive (someone is watching a progress bar),
/// so give up much faster than the unattended retry loop does.
#define PROVISION_AUTH_FAIL_LIMIT 2
#define PROVISION_ATTEMPT_LIMIT 3
#define PROVISION_TIMEOUT_US (30LL * 1000 * 1000)

static wifi_mgr_cb_t s_cb;
static esp_netif_t *s_ap_netif;
static volatile bool s_setup_mode;
static int s_auth_failures;
static char s_ip[16] = "0.0.0.0";
static char s_ap_ssid[33];
static char s_sta_ssid[33];
static char s_last_error[96];

// Live-trial provisioning (wifi_mgr_provision) state.
static portMUX_TYPE s_prov_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool s_provisioning;
static wifi_provision_cb_t s_prov_cb;
static char s_prov_ssid[33];
static char s_prov_pass[65];
static bool s_prov_was_setup; // setup AP was up when the trial started
static int s_prov_auth_failures;
static int s_prov_attempts;
static esp_timer_handle_t s_prov_timer;

static bool load_credentials(char ssid[33], char password[65])
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) {
        return false;
    }
    size_t ssid_len = 33, pass_len = 65;
    esp_err_t err = nvs_get_str(nvs, "ssid", ssid, &ssid_len);
    if (err == ESP_OK) {
        err = nvs_get_str(nvs, "pass", password, &pass_len);
    }
    nvs_close(nvs);
    return err == ESP_OK && ssid[0] != '\0';
}

/// Have the AP's DHCP server hand out the dongle itself as DNS server, so
/// the captive-portal DNS hijack sees every lookup a joining phone makes.
static void offer_self_as_dns(void)
{
    esp_netif_dns_info_t dns = {0};
    dns.ip.type = ESP_IPADDR_TYPE_V4;
    dns.ip.u_addr.ip4.addr = ESP_IP4TOADDR(192, 168, 4, 1);

    dhcps_offer_t offer = OFFER_DNS;
    esp_netif_dhcps_stop(s_ap_netif);
    esp_netif_dhcps_option(s_ap_netif, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER,
                           &offer, sizeof(offer));
    esp_netif_set_dns_info(s_ap_netif, ESP_NETIF_DNS_MAIN, &dns);
    esp_netif_dhcps_start(s_ap_netif);
}

/// Switch to setup mode (APSTA: AP for the portal, STA for scanning).
/// Safe to call before esp_wifi_start() and at runtime after auth failures.
static void enter_setup_mode(void)
{
    s_setup_mode = true;

    wifi_config_t ap_cfg = {0};
    strlcpy((char *)ap_cfg.ap.ssid, s_ap_ssid, sizeof(ap_cfg.ap.ssid));
    ap_cfg.ap.ssid_len = strlen(s_ap_ssid);
    ap_cfg.ap.channel = 1;
    ap_cfg.ap.max_connection = 2;
    ap_cfg.ap.authmode = WIFI_AUTH_OPEN;

    offer_self_as_dns();
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
}

static void retry_later(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(RETRY_DELAY_MS));
    // A provisioning trial owns the STA config while it runs; stay out.
    if (!s_setup_mode && !s_provisioning) {
        esp_wifi_connect();
    }
    vTaskDelete(NULL);
}

/// Disconnect reasons that mean "the credentials are wrong", as opposed to
/// "the network is unreachable right now".
static bool is_auth_failure(uint8_t reason)
{
    switch (reason) {
    case WIFI_REASON_AUTH_EXPIRE:
    case WIFI_REASON_MIC_FAILURE:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
        return true;
    default:
        return false;
    }
}

/* --- live-trial provisioning ---------------------------------------------- */

static void sta_config_apply(const char *ssid, const char *password)
{
    wifi_config_t sta_cfg = {0};
    strlcpy((char *)sta_cfg.sta.ssid, ssid, sizeof(sta_cfg.sta.ssid));
    strlcpy((char *)sta_cfg.sta.password, password, sizeof(sta_cfg.sta.password));
    strlcpy(s_sta_ssid, ssid, sizeof(s_sta_ssid));
    esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
}

/// Failed trial: put things back the way they were. In setup mode the AP
/// never went away, so there is nothing to do; a previously-joined dongle
/// reloads its stored credentials and hops back.
static void provision_revert(void)
{
    char ssid[33], password[65] = {0};
    if (!s_prov_was_setup && load_credentials(ssid, password)) {
        s_auth_failures = 0;
        sta_config_apply(ssid, password);
        esp_wifi_connect();
        if (s_cb) {
            s_cb(WIFI_MGR_CONNECTING);
        }
    }
}

/// Resolve the trial exactly once — the WiFi event loop and the timeout
/// timer can race to declare a result.
static void provision_finish(wifi_provision_result_t result, const char *ip)
{
    taskENTER_CRITICAL(&s_prov_mux);
    bool was_running = s_provisioning;
    s_provisioning = false;
    taskEXIT_CRITICAL(&s_prov_mux);
    if (!was_running) {
        return;
    }
    esp_timer_stop(s_prov_timer); // no-op from the timer's own callback

    if (result == WIFI_PROVISION_OK) {
        wifi_mgr_save_credentials(s_prov_ssid, s_prov_pass);
        if (s_prov_was_setup) {
            // Provisioned for real: the setup AP has done its job.
            s_setup_mode = false;
            esp_wifi_set_mode(WIFI_MODE_STA);
        }
        ESP_LOGI(TAG, "provisioned onto \"%s\" (%s)", s_prov_ssid, ip);
    } else {
        ESP_LOGW(TAG, "provisioning \"%s\" failed (%d); reverting", s_prov_ssid, result);
        provision_revert();
    }

    wifi_provision_cb_t cb = s_prov_cb;
    s_prov_cb = NULL;
    memset(s_prov_pass, 0, sizeof(s_prov_pass));
    if (cb) {
        cb(result, ip);
    }
}

static void provision_timeout(void *arg)
{
    provision_finish(WIFI_PROVISION_TIMEOUT, NULL);
}

static void on_provision_disconnected(const wifi_event_sta_disconnected_t *event)
{
    if (event->reason == WIFI_REASON_ASSOC_LEAVE) {
        return; // our own teardown of the previous connection
    }
    if (is_auth_failure(event->reason)) {
        if (++s_prov_auth_failures >= PROVISION_AUTH_FAIL_LIMIT) {
            provision_finish(WIFI_PROVISION_AUTH_FAILED, NULL);
            return;
        }
    } else if (event->reason == WIFI_REASON_NO_AP_FOUND) {
        provision_finish(WIFI_PROVISION_NOT_FOUND, NULL);
        return;
    } else if (++s_prov_attempts >= PROVISION_ATTEMPT_LIMIT) {
        provision_finish(WIFI_PROVISION_TIMEOUT, NULL);
        return;
    }
    esp_wifi_connect(); // interactive: retry immediately, the timer bounds us
}

esp_err_t wifi_mgr_provision(const char *ssid, const char *password, wifi_provision_cb_t cb)
{
    if (ssid == NULL || ssid[0] == '\0' || strlen(ssid) > 32 ||
        (password != NULL && strlen(password) > 64)) {
        return ESP_ERR_INVALID_ARG;
    }

    taskENTER_CRITICAL(&s_prov_mux);
    bool already = s_provisioning;
    if (!already) {
        s_provisioning = true;
    }
    taskEXIT_CRITICAL(&s_prov_mux);
    if (already) {
        return ESP_ERR_INVALID_STATE;
    }

    strlcpy(s_prov_ssid, ssid, sizeof(s_prov_ssid));
    strlcpy(s_prov_pass, password ? password : "", sizeof(s_prov_pass));
    s_prov_cb = cb;
    s_prov_was_setup = s_setup_mode;
    s_prov_auth_failures = 0;
    s_prov_attempts = 0;

    if (s_prov_timer == NULL) {
        const esp_timer_create_args_t args = {
            .callback = provision_timeout,
            .name = "wifi_prov",
        };
        ESP_ERROR_CHECK(esp_timer_create(&args, &s_prov_timer));
    }
    esp_timer_start_once(s_prov_timer, PROVISION_TIMEOUT_US);

    ESP_LOGI(TAG, "provisioning trial: joining \"%s\"", ssid);
    esp_wifi_disconnect(); // no-op unless we were on a network
    sta_config_apply(s_prov_ssid, s_prov_pass);
    esp_wifi_connect();
    return ESP_OK;
}

/* --- events ---------------------------------------------------------------- */

static void on_disconnected(const wifi_event_sta_disconnected_t *event)
{
    strlcpy(s_ip, "0.0.0.0", sizeof(s_ip));

    if (s_provisioning) {
        on_provision_disconnected(event);
        return;
    }
    if (s_setup_mode) {
        return;
    }

    if (is_auth_failure(event->reason)) {
        s_auth_failures++;
        ESP_LOGW(TAG, "auth failure %d/%d joining \"%s\" (reason %d)", s_auth_failures,
                 AUTH_FAIL_LIMIT, s_sta_ssid, event->reason);
        if (s_auth_failures >= AUTH_FAIL_LIMIT) {
            snprintf(s_last_error, sizeof(s_last_error),
                     "Couldn't join \"%s\" — wrong password?", s_sta_ssid);
            ESP_LOGE(TAG, "%s; entering setup mode (credentials kept)", s_last_error);
            enter_setup_mode(); // AP_START event fires the state callback
            return;
        }
    } else {
        // Router offline / out of range: not a credentials problem. Keep
        // trying forever so a router reboot heals without human help.
        s_auth_failures = 0;
        ESP_LOGW(TAG, "disconnected from \"%s\" (reason %d); retrying in %d ms", s_sta_ssid,
                 event->reason, RETRY_DELAY_MS);
    }

    if (s_cb) {
        s_cb(WIFI_MGR_CONNECTING);
    }
    // Retry from a scratch task: the event loop must not block.
    xTaskCreate(retry_later, "wifi_retry", 2048, NULL, 5, NULL);
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        if (!s_setup_mode) {
            esp_wifi_connect();
        }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        on_disconnected(data);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = data;
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&event->ip_info.ip));
        s_auth_failures = 0;
        s_last_error[0] = '\0';
        ESP_LOGI(TAG, "got ip %s", s_ip);
        if (s_provisioning) {
            // Commit + AP teardown happen before the state callback so main
            // sees a consistent "normally connected" dongle.
            provision_finish(WIFI_PROVISION_OK, s_ip);
        }
        if (s_cb) {
            s_cb(WIFI_MGR_CONNECTED);
        }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_START) {
        ESP_LOGI(TAG, "setup AP up: %s", s_ap_ssid);
        if (s_cb) {
            s_cb(WIFI_MGR_AP_MODE);
        }
    }
}

esp_err_t wifi_mgr_start(wifi_mgr_cb_t cb)
{
    s_cb = cb;

    // Deliberately the STA MAC, not the SoftAP MAC: the setup-hotspot name,
    // the mDNS hostname, and the API serial must all show the same XXXX.
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(s_ap_ssid, sizeof(s_ap_ssid), "Ember Link-%02X%02X", mac[4], mac[5]);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Both interfaces exist up front so we can flip modes at runtime
    // (auth-failure fallback) without re-plumbing netifs.
    esp_netif_create_default_wifi_sta();
    s_ap_netif = esp_netif_create_default_wifi_ap();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi_event, NULL));

    char password[65] = {0};
    if (load_credentials(s_sta_ssid, password)) {
        wifi_config_t sta_cfg = {0};
        strlcpy((char *)sta_cfg.sta.ssid, s_sta_ssid, sizeof(sta_cfg.sta.ssid));
        strlcpy((char *)sta_cfg.sta.password, password, sizeof(sta_cfg.sta.password));
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));
        ESP_LOGI(TAG, "connecting to \"%s\"", s_sta_ssid);
        if (s_cb) {
            s_cb(WIFI_MGR_CONNECTING);
        }
    } else {
        enter_setup_mode();
    }

    return esp_wifi_start();
}

bool wifi_mgr_in_setup(void)
{
    return s_setup_mode;
}

size_t wifi_mgr_scan(wifi_scan_result_t *out, size_t max)
{
    // Requires an active STA interface: true both in APSTA setup mode and
    // as a connected station.
    if (esp_wifi_scan_start(NULL, true /* block */) != ESP_OK) {
        return 0;
    }

    static wifi_ap_record_t records[SCAN_MAX_RECORDS];
    uint16_t count = SCAN_MAX_RECORDS;
    if (esp_wifi_scan_get_ap_records(&count, records) != ESP_OK) {
        return 0;
    }

    // Dedupe by SSID, keeping the strongest; records arrive sorted by RSSI.
    size_t n = 0;
    for (uint16_t i = 0; i < count && n < max; i++) {
        const char *ssid = (const char *)records[i].ssid;
        if (ssid[0] == '\0') {
            continue; // hidden network
        }
        bool seen = false;
        for (size_t j = 0; j < n; j++) {
            if (strcmp(out[j].ssid, ssid) == 0) {
                seen = true;
                break;
            }
        }
        if (seen) {
            continue;
        }
        strlcpy(out[n].ssid, ssid, sizeof(out[n].ssid));
        out[n].rssi = records[i].rssi;
        out[n].secure = records[i].authmode != WIFI_AUTH_OPEN;
        n++;
    }
    return n;
}

void wifi_mgr_last_error(char out[96])
{
    strlcpy(out, s_last_error, 96);
}

void wifi_mgr_configured_ssid(char out[33])
{
    char password[65];
    if (!load_credentials(out, password)) {
        out[0] = '\0';
    }
}

bool wifi_mgr_has_credentials(void)
{
    char ssid[33], password[65];
    return load_credentials(ssid, password);
}

esp_err_t wifi_mgr_save_credentials(const char *ssid, const char *password)
{
    if (ssid == NULL || ssid[0] == '\0' || strlen(ssid) > 32 ||
        (password != NULL && strlen(password) > 64)) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_str(nvs, "ssid", ssid);
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, "pass", password ? password : "");
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}

esp_err_t wifi_mgr_clear_credentials(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    nvs_erase_all(nvs);
    nvs_commit(nvs);
    nvs_close(nvs);
    return ESP_OK;
}

void wifi_mgr_get_ip(char out[16])
{
    strlcpy(out, s_ip, 16);
}

void wifi_mgr_get_ap_ssid(char out[33])
{
    strlcpy(out, s_ap_ssid, 33);
}
