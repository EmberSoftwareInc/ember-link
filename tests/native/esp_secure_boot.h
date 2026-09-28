#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
typedef struct { uint8_t key_digests[3][32]; unsigned num_digests; } esp_image_sig_public_key_digests_t;
esp_err_t esp_secure_boot_get_signature_blocks_for_running_app(bool, esp_image_sig_public_key_digests_t *);
