#pragma once
#include <stdint.h>
typedef unsigned TickType_t;
#define pdTRUE 1
#define pdPASS 1
#define pdMS_TO_TICKS(x) (x)

// LED tests verify each GPIO frame is serialized and delays are outside the lock.
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
void test_enter_critical(portMUX_TYPE *);
void test_exit_critical(portMUX_TYPE *);
#define portENTER_CRITICAL(p) test_enter_critical(p)
#define portEXIT_CRITICAL(p) test_exit_critical(p)
