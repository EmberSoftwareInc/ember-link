#include "display_settings.h"
#include "nvs.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned char saved[4], pending[4];
static size_t saved_size=4;
static bool exists, fail_open, fail_write, fail_commit;
esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *out) {
    assert(!strcmp(name,"linkdisplay")); (void)mode; *out=1;
    return fail_open ? ESP_FAIL : ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *out, size_t *size) {
    (void)h; assert(!strcmp(key,"prefs") && *size==4);
    if (!exists) return ESP_ERR_NVS_NOT_FOUND;
    memcpy(out,saved,saved_size); *size=saved_size; return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *data, size_t size) {
    (void)h; assert(!strcmp(key,"prefs") && size==4);
    if(fail_write) return ESP_FAIL;
    memcpy(pending,data,4); return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h) {
    (void)h; if(fail_commit) return ESP_FAIL;
    memcpy(saved,pending,4); saved_size=4; exists=true; return ESP_OK;
}
void nvs_close(nvs_handle_t h) { (void)h; }
int main(void) {
    display_settings_t s;
    display_settings_load(&s); assert(s.enabled && s.rotation==0 && s.led_enabled);
    assert(display_settings_save((display_settings_t){false,180,false})==ESP_OK);
    display_settings_load(&s); assert(!s.enabled && s.rotation==180 && !s.led_enabled);
    assert(display_settings_save((display_settings_t){true,90,true})==ESP_ERR_INVALID_ARG);
    fail_open=true; assert(display_settings_save((display_settings_t){true,0,true})==ESP_FAIL); fail_open=false;
    fail_write=true; assert(display_settings_save((display_settings_t){true,0,true})==ESP_FAIL); fail_write=false;
    fail_commit=true; assert(display_settings_save((display_settings_t){true,0,true})==ESP_FAIL); fail_commit=false;
    display_settings_load(&s); assert(!s.enabled && s.rotation==180 && !s.led_enabled);
    saved[0]=3; display_settings_load(&s); assert(s.enabled && s.rotation==0 && s.led_enabled);
    saved[0]=1; saved[1]=3; display_settings_load(&s); assert(s.enabled && s.rotation==0 && s.led_enabled);
    // Migration from installed 0.3.3 preferences preserves screen-off/rotation.
    saved_size=3; saved[0]=1; saved[1]=0; saved[2]=1;
    display_settings_load(&s); assert(!s.enabled && s.rotation==180 && s.led_enabled);
    s.led_enabled=false; assert(display_settings_save(s)==ESP_OK);
    display_settings_load(&s); assert(!s.enabled && s.rotation==180 && !s.led_enabled);
    saved_size=1; display_settings_load(&s); assert(s.enabled && s.led_enabled);
    assert(display_settings_save(display_settings_defaults())==ESP_OK);
    display_settings_load(&s); assert(s.enabled && s.rotation==0 && s.led_enabled);
    puts("Display settings tests passed (persistence, defaults, validation, NVS failures)");
}
