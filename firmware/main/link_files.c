#include "link_files.h"
#include "link_protocol.h"
#include "storage.h"
#include "esp_vfs_fat.h"
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define PATH_SIZE 160
static void path(char out[PATH_SIZE], const char *name)
{
    snprintf(out, PATH_SIZE, "%s/%s", storage_base_path(), name);
}
static bool exists(const char *p)
{
    struct stat s;
    return stat(p, &s) == 0;
}
static void hex(const uint8_t *bytes, char out[65])
{
    for (size_t i = 0; i < 32; i++)
        snprintf(out + i * 2, 3, "%02x", bytes[i]);
}

esp_err_t link_file_hash(const char *name, char out[65])
{
    if (!link_filename_valid(name))
        return ESP_ERR_INVALID_ARG;
    char p[PATH_SIZE];
    path(p, name);
    FILE *f = fopen(p, "rb");
    if (!f)
        return errno == ENOENT ? ESP_ERR_NOT_FOUND : ESP_FAIL;
    psa_hash_operation_t hash = PSA_HASH_OPERATION_INIT;
    psa_status_t st = psa_hash_setup(&hash, PSA_ALG_SHA_256);
    uint8_t buf[1024], digest[32];
    size_t n, got = 0;
    while (st == PSA_SUCCESS && (n = fread(buf, 1, sizeof(buf), f)) > 0)
        st = psa_hash_update(&hash, buf, n);
    bool failed = ferror(f);
    fclose(f);
    if (st == PSA_SUCCESS && !failed)
        st = psa_hash_finish(&hash, digest, sizeof(digest), &got);
    psa_hash_abort(&hash);
    if (st != PSA_SUCCESS || failed || got != 32)
        return ESP_FAIL;
    hex(digest, out);
    return ESP_OK;
}

// Marker records the destination before any old file is moved. Recovery keeps
// either the old backup or installed new file; it never deletes the only copy.
esp_err_t link_files_recover(void)
{
    char marker[PATH_SIZE], backup[PATH_SIZE], temp[PATH_SIZE], dest[PATH_SIZE];
    path(marker, "~link.commit");
    path(backup, "~link.backup");
    path(temp, "~link.upload");
    FILE *f = fopen(marker, "rb");
    if (f) {
        char name[LINK_MAX_FILENAME] = {0};
        size_t n = fread(name, 1, sizeof(name) - 1, f);
        int extra = fgetc(f);
        bool bad = ferror(f) || extra != EOF;
        fclose(f);
        if (bad || !n || !link_filename_valid(name))
            return ESP_ERR_INVALID_STATE;
        path(dest, name);
        if (!exists(dest) && exists(backup) && rename(backup, dest))
            return ESP_FAIL;
        if (exists(backup) && remove(backup))
            return ESP_FAIL;
        if (exists(temp) && remove(temp))
            return ESP_FAIL;
        if (remove(marker))
            return ESP_FAIL;
    } else {
        if (errno != ENOENT || exists(backup))
            return ESP_ERR_INVALID_STATE;
        if (exists(temp) && remove(temp))
            return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t link_file_begin(link_file_write_t *w, const char *name, size_t size)
{
    memset(w, 0, sizeof(*w));
    w->hash = (psa_hash_operation_t)PSA_HASH_OPERATION_INIT;
    if (!link_filename_valid(name) || !size || size > LINK_MAX_FILE_BYTES)
        return ESP_ERR_INVALID_ARG;
    esp_err_t err = link_files_recover();
    if (err != ESP_OK)
        return err;
    uint64_t total, free;
    if (esp_vfs_fat_info(storage_base_path(), &total, &free) != ESP_OK)
        return ESP_FAIL;
    if (free < (uint64_t)size + 65536)
        return ESP_ERR_NO_MEM;
    char p[PATH_SIZE];
    path(p, "~link.upload");
    w->file = fopen(p, "wb");
    if (!w->file)
        return ESP_FAIL;
    w->expected = size;
    strcpy(w->name, name);
    if (psa_hash_setup(&w->hash, PSA_ALG_SHA_256) != PSA_SUCCESS) {
        link_file_abort(w);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t link_file_write(link_file_write_t *w, const void *data, size_t len)
{
    if (!w->file || len > w->expected - w->written)
        return ESP_ERR_INVALID_SIZE;
    if (fwrite(data, 1, len, w->file) != len || psa_hash_update(&w->hash, data, len) != PSA_SUCCESS)
        return ESP_FAIL;
    w->written += len;
    return ESP_OK;
}

void link_file_abort(link_file_write_t *w)
{
    if (w->file) {
        fclose(w->file);
        w->file = NULL;
    }
    psa_hash_abort(&w->hash);
    char p[PATH_SIZE];
    path(p, "~link.upload");
    remove(p);
}

esp_err_t link_file_finish(link_file_write_t *w, const char *expected)
{
    if (!w->file || w->written != w->expected) {
        link_file_abort(w);
        return ESP_ERR_INVALID_SIZE;
    }
    uint8_t digest[32] = {0};
    size_t n = 0;
    char actual[65];
    psa_status_t st = psa_hash_finish(&w->hash, digest, sizeof(digest), &n);
    hex(digest, actual);
    if (st != PSA_SUCCESS || n != 32 ||
        (expected && (!link_sha256_valid(expected) || strcmp(expected, actual)))) {
        link_file_abort(w);
        return ESP_ERR_INVALID_CRC;
    }
    bool failed = fflush(w->file) != 0;
    if (!failed)
        failed = fsync(fileno(w->file)) != 0;
    if (fclose(w->file))
        failed = true;
    w->file = NULL;
    if (failed) {
        link_file_abort(w);
        return ESP_FAIL;
    }
    char marker[PATH_SIZE], backup[PATH_SIZE], temp[PATH_SIZE], dest[PATH_SIZE];
    path(marker, "~link.commit");
    path(backup, "~link.backup");
    path(temp, "~link.upload");
    path(dest, w->name);
    FILE *f = fopen(marker, "wb");
    if (!f)
        return ESP_FAIL;
    size_t len = strlen(w->name);
    failed = fwrite(w->name, 1, len, f) != len || fflush(f) != 0;
    if (!failed)
        failed = fsync(fileno(f)) != 0;
    if (fclose(f))
        failed = true;
    if (failed)
        return ESP_FAIL; // no original touched; fail closed on malformed marker.
    if (exists(dest) && rename(dest, backup))
        return ESP_FAIL;
    if (rename(temp, dest)) {
        (void)link_files_recover();
        return ESP_FAIL;
    }
    return link_files_recover();
}
