#pragma once
// Only injected into link_files.c, leaving the test harness's libc untouched.
int link_test_rename(const char *, const char *);
#define rename link_test_rename
