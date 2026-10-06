#pragma once
#include <stdio.h>
int local_test_rename(const char *source, const char *destination);
#define rename local_test_rename
