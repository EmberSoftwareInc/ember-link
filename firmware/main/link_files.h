#pragma once
#include <stdio.h>
#include "esp_err.h"
#include "psa/crypto.h"

// Caller owns operation gate AND storage_acquire() for every function here.
// Temporary/backup files use reserved names, invisible to inventory clients.
typedef struct {
    FILE *file;
    psa_hash_operation_t hash;
    size_t expected;
    size_t written;
    char name[128];
} link_file_write_t;

esp_err_t link_files_recover(void); // boot, before USB host sees the card
esp_err_t link_file_hash(const char *name, char out[65]);
esp_err_t link_file_begin(link_file_write_t *w, const char *name, size_t size);
esp_err_t link_file_write(link_file_write_t *w, const void *data, size_t len);
esp_err_t link_file_finish(link_file_write_t *w, const char *expected_sha256);
void link_file_abort(link_file_write_t *w);
