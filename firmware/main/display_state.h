#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef enum { DISPLAY_CARD_STARTING, DISPLAY_CARD_MISSING, DISPLAY_CARD_READY, DISPLAY_CARD_ERROR } display_card_t;
typedef enum { DISPLAY_WIFI_CONNECTING, DISPLAY_WIFI_SETUP, DISPLAY_WIFI_READY } display_wifi_t;
typedef enum { DISPLAY_CLOUD_OFF, DISPLAY_CLOUD_CONNECTING, DISPLAY_CLOUD_ONLINE, DISPLAY_CLOUD_ERROR } display_cloud_t;
typedef struct {
    display_card_t card;
    display_wifi_t wifi;
    display_cloud_t cloud;
    bool usb_setup, active, firmware, cloud_transfer;
    uint64_t done, total;
    uint32_t changed_at, notice_at, notice_duration;
    char filename[25], notice[25];
    bool notice_error;
} display_state_t;
typedef struct {
    char title[25], line1[25], line2[25];
    int percent; // -1 means no progress bar
    bool attention, dim;
} display_view_t;
void display_state_text(char out[25], const char *in);
void display_state_begin(display_state_t *s, bool firmware, bool cloud, const char *name, uint64_t total, uint32_t now);
void display_state_finish(display_state_t *s, bool success, const char *error, uint32_t now);
void display_state_notice(display_state_t *s, const char *text, bool error, uint32_t duration, uint32_t now);
void display_state_view(const display_state_t *s, uint32_t now, int rssi, display_view_t *out);
