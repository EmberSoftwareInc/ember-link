#include "storage.h"
#include "link_files.h"

#include <dirent.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_log.h"
#include "esp_mac.h"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdmmc_cmd.h"

#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_msc.h"
#include "tusb.h"

#include "app_version.h"
#include "board.h"

static const char *TAG = "storage";

#define BASE_PATH "/sd"
#define REPLUG_DELAY_MS 400

static tinyusb_msc_storage_handle_t s_storage = NULL;
static SemaphoreHandle_t s_lock;

static storage_file_t s_files[STORAGE_MAX_FILES];
static size_t s_file_count = 0;
static uint64_t s_total_bytes = 0;
static uint64_t s_free_bytes = 0;

/* --- USB descriptors ----------------------------------------------------- */

// Machines see only MSC. The composite MSC+CDC profile is explicitly enabled
// on a computer for setup; some machine USB hosts freeze on that profile.
enum {
    ITF_NUM_MSC = 0,
    ITF_NUM_CDC,      // CDC control (data interface is +1, claimed by the IAD)
    ITF_NUM_CDC_DATA,
    ITF_NUM_TOTAL,
};

#define TUSB_DESC_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_MSC_DESC_LEN + TUD_CDC_DESC_LEN)
#define EPNUM_MSC_OUT 0x01
#define EPNUM_MSC_IN 0x81
#define EPNUM_CDC_NOTIF 0x83
#define EPNUM_CDC_OUT 0x02
#define EPNUM_CDC_IN 0x82

static tusb_desc_device_t s_device_desc = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    // Espressif's VID/PID for development. A shipped product needs its own
    // (or a PID from Espressif's free PID program).
    .idVendor = 0x303A,
    .idProduct = 0x4002,
    .bcdDevice = 0x0100,
    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x03,
    .bNumConfigurations = 0x01,
};

// Advertise the existing 500 mA budget for Wi-Fi; host power compatibility
// still requires validation. Changing USB profiles does not reduce that load.
static uint8_t const s_fs_storage_config_desc[] = {
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, TUD_CONFIG_DESC_LEN + TUD_MSC_DESC_LEN,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 500),
    TUD_MSC_DESCRIPTOR(ITF_NUM_MSC, 0, EPNUM_MSC_OUT, EPNUM_MSC_IN, 64),
};

static uint8_t const s_fs_config_desc[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, TUSB_DESC_TOTAL_LEN,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 500),
    TUD_MSC_DESCRIPTOR(ITF_NUM_MSC, 0, EPNUM_MSC_OUT, EPNUM_MSC_IN, 64),
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 4, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),
};

#if (TUD_OPT_HIGH_SPEED)
static tusb_desc_device_qualifier_t s_device_qualifier = {
    .bLength = sizeof(tusb_desc_device_qualifier_t),
    .bDescriptorType = TUSB_DESC_DEVICE_QUALIFIER,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .bNumConfigurations = 0x01,
    .bReserved = 0,
};

static uint8_t const s_hs_storage_config_desc[] = {
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, TUD_CONFIG_DESC_LEN + TUD_MSC_DESC_LEN,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 500),
    TUD_MSC_DESCRIPTOR(ITF_NUM_MSC, 0, EPNUM_MSC_OUT, EPNUM_MSC_IN, 512),
};

static uint8_t const s_hs_config_desc[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, TUSB_DESC_TOTAL_LEN,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 500),
    TUD_MSC_DESCRIPTOR(ITF_NUM_MSC, 0, EPNUM_MSC_OUT, EPNUM_MSC_IN, 512),
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 4, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT, EPNUM_CDC_IN, 512),
};
#endif

static char s_serial[13]; // 12 hex MAC chars

static char const *s_string_desc[] = {
    (const char[]){0x09, 0x04}, // English
    "Ember",
    EMBER_LINK_NAME,
    s_serial,
    "Ember Link Setup", // 4: the CDC interface, shown by some port pickers
};

/* --- SD card ------------------------------------------------------------- */

static esp_err_t sd_card_init(sdmmc_card_t **out_card)
{
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 4;
    slot_config.clk = BOARD_SD_CLK_PIN;
    slot_config.cmd = BOARD_SD_CMD_PIN;
    slot_config.d0 = BOARD_SD_D0_PIN;
    slot_config.d1 = BOARD_SD_D1_PIN;
    slot_config.d2 = BOARD_SD_D2_PIN;
    slot_config.d3 = BOARD_SD_D3_PIN;
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    sdmmc_card_t *card = malloc(sizeof(sdmmc_card_t));
    if (card == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = host.init();
    if (err != ESP_OK) {
        free(card);
        return err;
    }
    err = sdmmc_host_init_slot(host.slot, &slot_config);
    if (err == ESP_OK) {
        err = sdmmc_card_init(&host, card);
    }
    if (err != ESP_OK) {
        sdmmc_host_deinit();
        free(card);
        return err;
    }

    sdmmc_card_print_info(stdout, card);
    *out_card = card;
    return ESP_OK;
}

/* --- "START HERE" pointer ------------------------------------------------- */

#define START_HERE_NAME "START HERE.html"

/// Written to an unprovisioned card so the out-of-box volume explains
/// itself; a plain HTML redirect stays current forever, unlike a bundled
/// installer that would age in a warehouse. Removed once provisioned.
/// Requires the card to be APP-mounted.
static void sync_start_here(bool provisioned)
{
    char path[sizeof(BASE_PATH) + sizeof(START_HERE_NAME) + 1];
    snprintf(path, sizeof(path), BASE_PATH "/%s", START_HERE_NAME);

    if (provisioned) {
        remove(path); // harmless if absent
        return;
    }
    FILE *f = fopen(path, "w");
    if (f == NULL) {
        ESP_LOGW(TAG, "could not write %s", START_HERE_NAME);
        return;
    }
    fprintf(f,
            "<!doctype html><meta charset=\"utf-8\">"
            "<meta http-equiv=\"refresh\" content=\"0;url=https://connect.emberdesign.net?serial=%s\">"
            "<title>Set up Ember Link</title>"
            "<p>Taking you to the Ember Link setup&hellip; "
            "<a href=\"https://connect.emberdesign.net?serial=%s\">Click here</a> "
            "if nothing happens.</p>",
            s_serial, s_serial);
    fclose(f);
    ESP_LOGI(TAG, "wrote %s (unprovisioned dongle)", START_HERE_NAME);
}

/* --- Cache --------------------------------------------------------------- */

// Requires the card to be APP-mounted.
static void refresh_cache(void)
{
    s_file_count = 0;
    s_total_bytes = 0;
    s_free_bytes = 0;

    esp_err_t err = esp_vfs_fat_info(BASE_PATH, &s_total_bytes, &s_free_bytes);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "fat info failed: %s", esp_err_to_name(err));
    }

    DIR *dir = opendir(BASE_PATH);
    if (dir == NULL) {
        ESP_LOGW(TAG, "opendir(%s) failed", BASE_PATH);
        return;
    }
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && s_file_count < STORAGE_MAX_FILES) {
        if (entry->d_type != DT_REG || entry->d_name[0] == '.' || entry->d_name[0] == '~') {
            continue;
        }
        if (strcasecmp(entry->d_name, START_HERE_NAME) == 0) {
            continue; // ours, not a design
        }
        storage_file_t *f = &s_files[s_file_count];
        strlcpy(f->name, entry->d_name, sizeof(f->name));

        char path[sizeof(BASE_PATH) + STORAGE_MAX_NAME];
        snprintf(path, sizeof(path), BASE_PATH "/%s", f->name);
        struct stat st;
        f->size = (stat(path, &st) == 0) ? (uint64_t)st.st_size : 0;
        s_file_count++;
    }
    closedir(dir);
    ESP_LOGI(TAG, "cache: %u files, %llu/%llu bytes free", (unsigned)s_file_count,
             (unsigned long long)s_free_bytes, (unsigned long long)s_total_bytes);
}

/* --- Public API ---------------------------------------------------------- */

esp_err_t storage_init(bool provisioned, bool setup_mode, void (*waiting_for_card_cb)(void))
{
    s_lock = xSemaphoreCreateMutex();

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(s_serial, sizeof(s_serial), "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    sdmmc_card_t *card = NULL;
    while (sd_card_init(&card) != ESP_OK) {
        ESP_LOGE(TAG, "no usable SD card; retrying in 3s");
        if (waiting_for_card_cb) {
            waiting_for_card_cb();
        }
        vTaskDelay(pdMS_TO_TICKS(3000));
    }

    // Start APP-mounted so we can build the cache before going live on USB.
    tinyusb_msc_storage_config_t storage_cfg = {
        .mount_point = TINYUSB_MSC_STORAGE_MOUNT_APP,
        .fat_fs = {
            .base_path = BASE_PATH,
            .config.max_files = 4,
            .format_flags = 0,
        },
        .medium.card = card,
    };
    esp_err_t err = tinyusb_msc_new_storage_sdmmc(&storage_cfg, &s_storage);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "msc storage create failed: %s", esp_err_to_name(err));
        return err;
    }

    err = link_files_recover();
    if (err != ESP_OK) return err;
    sync_start_here(provisioned);
    refresh_cache();

    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG();
    s_device_desc.bDeviceClass = setup_mode ? TUSB_CLASS_MISC : 0;
    s_device_desc.bDeviceSubClass = setup_mode ? MISC_SUBCLASS_COMMON : 0;
    s_device_desc.bDeviceProtocol = setup_mode ? MISC_PROTOCOL_IAD : 0;
    tusb_cfg.descriptor.device = &s_device_desc;
    tusb_cfg.descriptor.full_speed_config = setup_mode ? s_fs_config_desc : s_fs_storage_config_desc;
    tusb_cfg.descriptor.string = s_string_desc;
    tusb_cfg.descriptor.string_count = sizeof(s_string_desc) / sizeof(s_string_desc[0]);
#if (TUD_OPT_HIGH_SPEED)
    s_device_qualifier.bDeviceClass = s_device_desc.bDeviceClass;
    s_device_qualifier.bDeviceSubClass = s_device_desc.bDeviceSubClass;
    s_device_qualifier.bDeviceProtocol = s_device_desc.bDeviceProtocol;
    tusb_cfg.descriptor.high_speed_config = setup_mode ? s_hs_config_desc : s_hs_storage_config_desc;
    tusb_cfg.descriptor.qualifier = &s_device_qualifier;
#endif
    err = tinyusb_driver_install(&tusb_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "tinyusb install failed: %s", esp_err_to_name(err));
        return err;
    }

    err = tinyusb_msc_set_storage_mount_point(s_storage, TINYUSB_MSC_STORAGE_MOUNT_USB);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "expose to USB failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "SD card exposed over USB (serial %s)", s_serial);
    return ESP_OK;
}

esp_err_t storage_acquire(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = tinyusb_msc_set_storage_mount_point(s_storage, TINYUSB_MSC_STORAGE_MOUNT_APP);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "acquire failed: %s", esp_err_to_name(err));
        xSemaphoreGive(s_lock);
    }
    return err;
}

esp_err_t storage_release(void)
{
    refresh_cache();

    esp_err_t err = tinyusb_msc_set_storage_mount_point(s_storage, TINYUSB_MSC_STORAGE_MOUNT_USB);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "release failed: %s", esp_err_to_name(err));
    }

    // Simulate unplug/replug so the host machine re-reads the FAT. Machines
    // cache directory contents from insertion time; without this they would
    // never see the new file.
    if (tud_inited()) {
        tud_disconnect();
        vTaskDelay(pdMS_TO_TICKS(REPLUG_DELAY_MS));
        tud_connect();
    }

    xSemaphoreGive(s_lock);
    return err;
}

const char *storage_base_path(void)
{
    return BASE_PATH;
}

size_t storage_cached_files(storage_file_t *out, size_t max)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    size_t n = (s_file_count < max) ? s_file_count : max;
    memcpy(out, s_files, n * sizeof(storage_file_t));
    xSemaphoreGive(s_lock);
    return n;
}

void storage_cached_stats(uint64_t *total_bytes, uint64_t *free_bytes)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *total_bytes = s_total_bytes;
    *free_bytes = s_free_bytes;
    xSemaphoreGive(s_lock);
}
