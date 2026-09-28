#include "display.h"
#include "ota.h"
#include "operation.h"

#include <string.h>

#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ota";

// The app descriptor sits right after the image and first-segment headers;
// buffer that much so the identity check works no matter how the incoming
// stream is chunked.
#define HEADER_LEN                                                              \
    (sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) +          \
     sizeof(esp_app_desc_t))

static esp_ota_handle_t s_handle;
static const esp_partition_t *s_target;
static bool s_in_progress;
static bool s_prepared;
static size_t s_expected, s_written;
static char s_expected_version[32];
static bool s_header_checked;
static size_t s_header_have;
static uint8_t s_header[HEADER_LEN];

void ota_get_status(ota_status_t *out)
{
    memset(out, 0, sizeof(*out));

    const esp_app_desc_t *desc = esp_app_get_description();
    strlcpy(out->version, desc->version, sizeof(out->version));

    const esp_partition_t *running = esp_ota_get_running_partition();
    strlcpy(out->slot, running->label, sizeof(out->slot));

    esp_ota_img_states_t state;
    out->pending_verify = esp_ota_get_state_partition(running, &state) == ESP_OK &&
                          state == ESP_OTA_IMG_PENDING_VERIFY;

    const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
    out->max_image_size = next != NULL ? next->size : 0;
}

esp_err_t ota_begin(size_t image_size)
{
    if (s_in_progress || s_prepared) {
        return ESP_ERR_INVALID_STATE;
    }
    s_target = esp_ota_get_next_update_partition(NULL);
    if (s_target == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    if (!image_size || image_size > s_target->size) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (!operation_begin()) return ESP_ERR_INVALID_STATE;
    esp_err_t err = esp_ota_begin(s_target, OTA_WITH_SEQUENTIAL_WRITES, &s_handle);
    if (err != ESP_OK) {
        operation_end();
        ESP_LOGE(TAG, "esp_ota_begin: %s", esp_err_to_name(err));
        return err;
    }
    s_in_progress = true;
    display_begin(true, false, NULL, image_size);
    s_expected = image_size;
    s_written = 0;
    s_expected_version[0] = 0;
    s_header_checked = false;
    s_header_have = 0;
    ESP_LOGI(TAG, "update started -> %s (%u bytes incoming)", s_target->label,
             (unsigned)image_size);
    return ESP_OK;
}

// Reject images that are obviously not ours before flashing much: correct
// image magic, right chip, and the project name baked in by the build.
// (The cryptographic check happens in ota_finish; this exists to give a
// clear error for honest mistakes like pushing a .bin from another project.)
static esp_err_t check_header(void)
{
    const esp_image_header_t *img = (const esp_image_header_t *)s_header;
    const esp_app_desc_t *app =
        (const esp_app_desc_t *)(s_header + sizeof(esp_image_header_t) +
                                 sizeof(esp_image_segment_header_t));

    if (img->magic != ESP_IMAGE_HEADER_MAGIC || img->chip_id != ESP_CHIP_ID_ESP32S3 ||
        app->magic_word != ESP_APP_DESC_MAGIC_WORD ||
        strncmp(app->project_name, "ember-link", sizeof(app->project_name)) != 0 ||
        !memchr(app->version, 0, sizeof(app->version)) ||
        (s_expected_version[0] && strcmp(app->version, s_expected_version))) {
        ESP_LOGE(TAG, "image is not Ember Link firmware");
        return ESP_ERR_INVALID_ARG;
    }
    ESP_LOGI(TAG, "incoming image: %s %s", app->project_name, app->version);
    return ESP_OK;
}

esp_err_t ota_write(const void *data, size_t len)
{
    if (!s_in_progress) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!data || len > s_expected - s_written) return ESP_ERR_INVALID_SIZE;
    if (!s_header_checked) {
        size_t take = HEADER_LEN - s_header_have;
        if (take > len) {
            take = len;
        }
        memcpy(s_header + s_header_have, data, take);
        s_header_have += take;
        if (s_header_have == HEADER_LEN) {
            esp_err_t err = check_header();
            if (err != ESP_OK) {
                return err;
            }
            s_header_checked = true;
        }
    }
    esp_err_t err = esp_ota_write(s_handle, data, len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_write: %s", esp_err_to_name(err));
    }
    if (err == ESP_OK) { s_written += len; display_progress(s_written); }
    return err;
}

esp_err_t ota_prepare(void)
{
    if (!s_in_progress) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_header_checked || s_written != s_expected) {
        ota_abort();
        return ESP_ERR_INVALID_ARG; // shorter than any real image
    }
    s_in_progress = false;

    // Verifies the SHA-256 and the RSA signature block against the public
    // key in the running app (SECURE_SIGNED_ON_UPDATE_NO_SECURE_BOOT).
    esp_err_t err = esp_ota_end(s_handle);
    if (err != ESP_OK) {
        display_finish(false, "Update rejected");
        operation_end();
        ESP_LOGE(TAG, "esp_ota_end: %s", esp_err_to_name(err));
        return err;
    }
    s_prepared = true;
    return ESP_OK;
}

esp_err_t ota_activate(void)
{
    if (!s_prepared) return ESP_ERR_INVALID_STATE;
    s_prepared = false;
    esp_err_t err = esp_ota_set_boot_partition(s_target);
    if (err != ESP_OK) {
        display_finish(false, "Update failed");
        operation_end();
        ESP_LOGE(TAG, "esp_ota_set_boot_partition: %s", esp_err_to_name(err));
        return err;
    }
    display_finish(true, NULL);
    ESP_LOGI(TAG, "update verified; %s boots next", s_target->label);
    return ESP_OK;
}

esp_err_t ota_finish(void)
{
    esp_err_t err = ota_prepare();
    return err == ESP_OK ? ota_activate() : err;
}

esp_err_t ota_expect_version(const char *version)
{
    if (!s_in_progress || s_written || !version || strlen(version) >= sizeof(s_expected_version))
        return ESP_ERR_INVALID_STATE;
    strcpy(s_expected_version, version);
    return ESP_OK;
}

void ota_abort(void)
{
    if (s_prepared || s_in_progress) display_finish(false, "Update stopped");
    if (s_prepared) { s_prepared = false; operation_end(); }
    if (s_in_progress) {
        s_in_progress = false;
        esp_ota_abort(s_handle);
        operation_end();
        ESP_LOGW(TAG, "update aborted");
    }
}

void ota_confirm(void)
{
    static bool s_confirmed;
    if (s_confirmed) {
        return;
    }

    esp_ota_img_states_t state;
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (esp_ota_get_state_partition(running, &state) == ESP_OK &&
        state == ESP_OTA_IMG_PENDING_VERIFY) {
        if (esp_ota_mark_app_valid_cancel_rollback() != ESP_OK) {
            ESP_LOGE(TAG, "could not confirm image; health guard remains armed");
            return;
        }
        display_notice("Update ready", false);
        ESP_LOGI(TAG, "updated firmware confirmed healthy; rollback cancelled");
    }
    s_confirmed = true;
}

static void boot_guard(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(90000));
    ota_status_t status;
    ota_get_status(&status);
    if (status.pending_verify) {
        ESP_LOGE(TAG, "local services did not become healthy; rolling back");
        esp_ota_mark_app_invalid_rollback_and_reboot();
    }
    vTaskDelete(NULL);
}

esp_err_t ota_start_boot_guard(void)
{
    ota_status_t status;
    ota_get_status(&status);
    if (!status.pending_verify) return ESP_OK;
    return xTaskCreate(boot_guard, "ota_health", 3072, NULL, 5, NULL) == pdPASS
        ? ESP_OK : ESP_ERR_NO_MEM;
}
