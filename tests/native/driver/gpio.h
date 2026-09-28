#pragma once
#include <stdint.h>
#define GPIO_MODE_OUTPUT 1
typedef struct { uint64_t pin_bit_mask; int mode; } gpio_config_t;
int gpio_config(const gpio_config_t *);
int gpio_set_level(int pin, unsigned level);
