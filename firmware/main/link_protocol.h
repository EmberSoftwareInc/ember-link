#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LINK_MAX_FILE_BYTES (64U * 1024U * 1024U)
#define LINK_MAX_FILENAME 128
#define LINK_MAX_ID 64
#define LINK_MAX_URL 2048

// Portable validation shared by firmware and native tests. Paths are root-only.
bool link_filename_valid(const char *name);
bool link_id_valid(const char *value);
bool link_sha256_valid(const char *value);
bool link_token_valid(const char *value);
bool link_host_valid(const char *host);
bool link_https_url_valid(const char *url, const char *host);
bool link_api_base_valid(const char *url);
bool link_integer_valid(double value, uint64_t minimum, uint64_t maximum);
