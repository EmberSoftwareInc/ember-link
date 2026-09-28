#include "display_state.h"
#include "display_render.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    display_state_t s={.card=DISPLAY_CARD_READY,.wifi=DISPLAY_WIFI_READY};
    display_view_t v;
    display_state_view(&s,0,-50,&v);
    assert(!strcmp(v.title,"Ready") && strstr(v.line2,"Cloud off") && !v.attention);
    display_state_begin(&s,false,true,"square.pes",100,100);
    s.done=100; display_state_view(&s,101,-50,&v);
    assert(!strcmp(v.title,"Receiving") && v.percent==99 && strstr(v.line2,"Cloud"));
    display_state_finish(&s,true,NULL,102); display_state_view(&s,103,-50,&v);
    assert(!strcmp(v.title,"Saved to Link") && !strcmp(v.line1,"square.pes"));
    display_state_view(&s,12102,-50,&v); assert(!strcmp(v.title,"Ready"));
    display_state_begin(&s,false,false,"triangle.pes",UINT64_MAX,13000);
    s.done=UINT64_MAX; display_state_view(&s,13001,-50,&v); assert(v.percent==99);
    display_state_finish(&s,false,"Card full",13002); display_state_view(&s,13003,-50,&v);
    assert(v.attention && !v.dim && !strcmp(v.title,"Card full"));
    s.usb_setup=true; display_state_view(&s,34000,-50,&v); assert(!strcmp(v.title,"USB setup"));
    s.card=DISPLAY_CARD_MISSING; display_state_view(&s,35000,-50,&v); assert(!strcmp(v.title,"Check SD card"));
    s.card=DISPLAY_CARD_READY; s.usb_setup=false; s.wifi=DISPLAY_WIFI_CONNECTING;
    display_state_view(&s,50000,0,&v); assert(!strcmp(v.title,"Wi-Fi offline") && v.dim);
    display_state_begin(&s,true,false,NULL,100,UINT32_MAX-5000u);
    display_state_view(&s,UINT32_MAX,-60,&v); assert(!v.dim && !strcmp(v.line1,"Keep plugged in"));
    display_state_finish(&s,true,NULL,UINT32_MAX-1000u);
    display_state_view(&s,1000,-60,&v); assert(!strcmp(v.title,"Restarting"));
    display_state_view(&s,12000,-60,&v); assert(!strcmp(v.title,"Wi-Fi offline"));
    char name[25]; display_state_text(name,"abcdefghijklmnopqrstuvwxyz.pes");
    assert(strlen(name)==24 && !strcmp(name+21,"..."));
    display_state_text(name,"bad\nname\xff"); assert(!strcmp(name,"bad?name?"));
    uint16_t guarded[160*80+2]; guarded[0]=0xa55a; guarded[160*80+1]=0x5aa5;
    memset(v.title,'W',24); v.title[24]=0;
    memset(v.line1,'?',24); v.line1[24]=0;
    v.percent=99; display_render(guarded+1,&v);
    assert(guarded[0]==0xa55a && guarded[160*80+1]==0x5aa5);
    puts("Display status tests passed (commit truth, error priority, privacy, dimming, rollover)");
}
