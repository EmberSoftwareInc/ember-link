#pragma once
#include <stddef.h>
#define ESP_PARTITION_TYPE_DATA 1
#define ESP_PARTITION_SUBTYPE_DATA_NVS 2
typedef struct { char label[17]; size_t size; } esp_partition_t;
const esp_partition_t *esp_partition_find_first(int, int, const char *);
