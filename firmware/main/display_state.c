#include "display_state.h"
#include <stdio.h>
#include <string.h>

void display_state_text(char out[25], const char *in)
{
    memset(out, 0, 25);
    if (!in) return;
    size_t i = 0;
    while (i < 24 && in[i]) {
        unsigned char c = (unsigned char)in[i];
        out[i] = c >= 32 && c <= 126 ? (char)c : '?';
        i++;
    }
    if (i == 24 && in[i]) memcpy(out + 21, "...", 3);
}
void display_state_begin(display_state_t *s, bool firmware, bool cloud, const char *name, uint64_t total, uint32_t now)
{
    s->active = true; s->firmware = firmware; s->cloud_transfer = cloud;
    s->done = 0; s->total = total; s->notice_duration = 0; s->changed_at = now;
    display_state_text(s->filename, name);
}
void display_state_notice(display_state_t *s, const char *text, bool error, uint32_t duration, uint32_t now)
{
    display_state_text(s->notice, text); s->notice_error = error;
    s->notice_at = now; s->notice_duration = duration; s->changed_at = now;
}
void display_state_finish(display_state_t *s, bool success, const char *error, uint32_t now)
{
    s->active = false;
    display_state_notice(s, success ? (s->firmware ? "Restarting" : "Saved to Link") : error,
                         !success, success ? 12000 : 20000, now);
}
void display_state_view(const display_state_t *s, uint32_t now, int rssi, display_view_t *v)
{
    memset(v, 0, sizeof(*v)); v->percent = -1;
    if (s->card != DISPLAY_CARD_READY) {
        display_state_text(v->title, s->card == DISPLAY_CARD_STARTING ? "Starting" : "Check SD card");
        display_state_text(v->line1, s->card == DISPLAY_CARD_STARTING ? "Preparing storage" : "Insert a FAT32 card");
        display_state_text(v->line2, s->usb_setup ? "Open installer to repair" : "Double BOOT for setup");
        v->attention = s->card != DISPLAY_CARD_STARTING;
    } else if (s->active) {
        display_state_text(v->title, s->firmware ? "Updating" : "Receiving");
        display_state_text(v->line1, s->firmware ? "Keep plugged in" : s->filename);
        unsigned percent = !s->total ? 0 : s->done >= s->total ? 99 : (unsigned)((double)s->done / (double)s->total * 100.0);
        if (percent > 99) percent = 99; // 100% is not success until verification/commit
        v->percent = (int)percent;
        snprintf(v->line2, sizeof(v->line2), "%s  %u%%", s->firmware ? "Firmware" : s->cloud_transfer ? "Cloud" : "Local", percent);
    } else if (s->notice_duration && (uint32_t)(now - s->notice_at) < s->notice_duration) {
        display_state_text(v->title, s->notice);
        display_state_text(v->line1, s->firmware ? "Keep plugged in" : s->filename);
        display_state_text(v->line2, s->notice_error ? "Check Ember or Bridge" : !strcmp(s->notice, "Ready to pair") ? "Open Ember Bridge" : "");
        v->attention = s->notice_error;
    } else if (s->usb_setup) {
        display_state_text(v->title, "USB setup");
        display_state_text(v->line1, "Open Ember or Bridge");
        display_state_text(v->line2, "Unplug to exit setup");
    } else if (s->wifi != DISPLAY_WIFI_READY) {
        display_state_text(v->title, s->wifi == DISPLAY_WIFI_SETUP ? "Wi-Fi setup" : "Wi-Fi offline");
        display_state_text(v->line1, s->wifi == DISPLAY_WIFI_SETUP ? "Use Ember or Bridge" : "Reconnecting...");
        display_state_text(v->line2, "USB files still usable");
    } else {
        display_state_text(v->title, "Ready");
        snprintf(v->line1, sizeof(v->line1), "Wi-Fi %s", rssi == 0 ? "connected" : rssi > -60 ? "strong" : rssi > -75 ? "fair" : "weak");
        display_state_text(v->line2, s->cloud == DISPLAY_CLOUD_OFF ? "Local ready | Cloud off" :
                           s->cloud == DISPLAY_CLOUD_ONLINE ? "Local + cloud ready" :
                           s->cloud == DISPLAY_CLOUD_UNCLAIMED ? "Not linked to an account" :
                           s->cloud == DISPLAY_CLOUD_ERROR ? "Local ready | Cloud down" : "Cloud connecting...");
    }
    v->dim = !s->active && !v->attention && (uint32_t)(now - s->changed_at) >= 30000;
}
