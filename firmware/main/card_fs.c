#include "card_fs.h"
#include "card_layout.h"
#include "ff.h"
#include "diskio_impl.h"
#include "diskio_sdmmc.h"
#include <stdlib.h>
#include <string.h>

static esp_err_t run(sdmmc_card_t *card, card_info_t *info, bool format, bool rename)
{
    memset(info, 0, sizeof(*info));
    if (!card || sdmmc_get_status(card) != ESP_OK) return ESP_ERR_NOT_FOUND;
    info->present = true;
    info->capacity_bytes = (uint64_t)card->csd.capacity * card->csd.sector_size;
    info->can_format = card_layout_supported(card->csd.capacity, card->csd.sector_size);
    if (format && !info->can_format) return ESP_ERR_NOT_SUPPORTED;
    FATFS *fs = calloc(1, sizeof(*fs));
    uint8_t *work = format ? malloc(4096) : NULL;
    if (!fs || (format && !work)) { free(fs); free(work); return ESP_ERR_NO_MEM; }
    BYTE drive = 0xff;
    esp_err_t err = ff_diskio_get_drive(&drive);
    if (err != ESP_OK) { free(fs); free(work); return err; }
    ff_diskio_register_sdmmc(drive, card);
    PARTITION saved = VolToPart[drive];
    VolToPart[drive] = (PARTITION){drive, 0};
    char path[3] = {(char)('0' + drive), ':', 0};
    err = ESP_FAIL;
    if (format) {
        // Replace the layout rather than formatting an arbitrary old partition.
        card_layout_mbr(work, card->csd.capacity, card->csd.sector_size);
        if (sdmmc_write_sectors(card, work, 0, 1) != ESP_OK) goto done;
        // Remove stale primary/backup GPT metadata. This is a quick format,
        // not secure erasure; old file contents may remain recoverable.
        memset(work, 0, 4096);
        for (uint32_t sector = 1; sector < 34; sector++)
            if (sdmmc_write_sectors(card, work, sector, 1) != ESP_OK) goto done;
        for (uint32_t sector = card->csd.capacity - 33; sector < card->csd.capacity; sector++)
            if (sdmmc_write_sectors(card, work, sector, 1) != ESP_OK) goto done;
        VolToPart[drive].pt = 1;
        MKFS_PARM options = {.fmt = FM_FAT32, .n_fat = 2, .align = 1,
                             .au_size = card_layout_cluster_size(card->csd.capacity)};
        if (f_mkfs(path, &options, work, 4096) != FR_OK) goto done;
    }
    if (f_mount(fs, path, 1) != FR_OK) goto done;
    info->readable = fs->fs_type == FS_FAT16 || fs->fs_type == FS_FAT32;
    info->fat32 = fs->fs_type == FS_FAT32;
    if (!info->readable) goto done;
    char label[48] = {0};
    if (f_getlabel(path, label, NULL) != FR_OK) goto done;
    if (format || rename) {
        if (!info->fat32) goto done;
        if (strcmp(label, CARD_VOLUME_LABEL)) {
            char name[] = "0:" CARD_VOLUME_LABEL; name[0] = path[0];
            if (f_setlabel(name) != FR_OK) goto done;
        }
        // The label is a root-directory entry; f_setlabel flushes metadata.
        // Remount and read it back before claiming success. No raw sector or
        // partition writes are used for rename, and f_mkfs is never called.
        if (f_mount(NULL, path, 0) != FR_OK || f_mount(fs, path, 1) != FR_OK ||
            f_getlabel(path, label, NULL) != FR_OK || strcmp(label, CARD_VOLUME_LABEL)) goto done;
    }
    for (size_t i = 0; i < sizeof(info->label) - 1 && label[i]; i++) {
        unsigned char ch = (unsigned char)label[i];
        info->label[i] = ch >= 32 && ch <= 126 ? (char)ch : '?';
    }
    if (format) {
        // Verify actual write, flush, unmount, remount and readback.
        char filename[] = "0:/ELTEST.TMP"; filename[0] = path[0];
        FIL file = {0};
        UINT count = 0;
        const char check[] = "Ember Link card verification";
        char readback[sizeof(check)] = {0};
        if (f_open(&file, filename, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) goto done;
        bool ok = f_write(&file, check, sizeof(check), &count) == FR_OK && count == sizeof(check);
        if (f_sync(&file) != FR_OK) ok = false;
        if (f_close(&file) != FR_OK || !ok) goto done;
        if (f_mount(NULL, path, 0) != FR_OK || f_mount(fs, path, 1) != FR_OK) goto done;
        if (f_open(&file, filename, FA_READ) != FR_OK) goto done;
        ok = f_read(&file, readback, sizeof(readback), &count) == FR_OK &&
             count == sizeof(check) && memcmp(check, readback, sizeof(check)) == 0;
        if (f_close(&file) != FR_OK || !ok || f_unlink(filename) != FR_OK) goto done;
    }
    err = info->readable ? ESP_OK : ESP_FAIL;
done:
    f_mount(NULL, path, 0);
    VolToPart[drive] = saved;
    ff_diskio_unregister(drive);
    free(fs); free(work);
    if (err != ESP_OK) info->readable = info->fat32 = false;
    return err;
}
esp_err_t card_fs_inspect(sdmmc_card_t *card, card_info_t *info) { return run(card, info, false, false); }
esp_err_t card_fs_format(sdmmc_card_t *card, card_info_t *info) { return run(card, info, true, false); }

esp_err_t card_fs_rename(sdmmc_card_t *card, card_info_t *info) { return run(card, info, false, true); }
