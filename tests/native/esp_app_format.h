#pragma once
#include <stdint.h>
#define ESP_IMAGE_HEADER_MAGIC 0xe9
#define ESP_CHIP_ID_ESP32S3 9
typedef struct { uint32_t magic; uint32_t chip_id; } esp_image_header_t;
typedef struct { uint32_t length; } esp_image_segment_header_t;
