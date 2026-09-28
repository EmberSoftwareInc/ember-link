// Business-logic suites run without display hardware; the pure model has its own suite.
#include "display.h"
void display_begin(bool firmware, bool cloud, const char *name, uint64_t total) { (void)firmware; (void)cloud; (void)name; (void)total; }
void display_progress(uint64_t written) { (void)written; }
void display_finish(bool success, const char *error) { (void)success; (void)error; }
void display_notice(const char *text, bool error) { (void)text; (void)error; }
void display_cloud(display_cloud_t state) { (void)state; }
