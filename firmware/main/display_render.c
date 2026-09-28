#include "display_render.h"
#include <string.h>
// Original compact 5x7 uppercase glyphs; lowercase is displayed uppercase.
static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-_%:/+|?";
static const uint8_t glyphs[][7] = {
 {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},
 {30,17,17,17,17,17,30},{31,16,16,30,16,16,31},{31,16,16,30,16,16,16},
 {14,17,16,23,17,17,15},{17,17,17,31,17,17,17},{14,4,4,4,4,4,14},
 {7,2,2,2,18,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
 {17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},
 {30,17,17,30,16,16,16},{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},
 {15,16,16,14,1,1,30},{31,4,4,4,4,4,4},{17,17,17,17,17,17,14},
 {17,17,17,17,17,10,4},{17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
 {17,17,10,4,4,4,4},{31,1,2,4,8,16,31},
 {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},
 {30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
 {14,16,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},
 {14,17,17,15,1,1,14},
 {0,0,0,0,0,4,4},{0,0,0,31,0,0,0},{0,0,0,0,0,0,31},
 {25,25,2,4,8,19,19},{0,4,4,0,4,4,0},{1,2,2,4,8,8,16},
 {0,4,4,31,4,4,0},{4,4,4,4,4,4,4},{14,17,1,2,4,0,4}
};

_Static_assert(sizeof(glyphs)/sizeof(glyphs[0]) == sizeof(alphabet)-1, "glyph map");
static uint16_t wire_color(uint16_t c) { return (uint16_t)((c << 8) | (c >> 8)); }
static void rect(uint16_t *pixels, int x, int y, int w, int h, uint16_t color) {
    for (int yy=y; yy<y+h && yy<80; yy++) for (int xx=x; xx<x+w && xx<160; xx++)
        if (xx>=0 && yy>=0) pixels[yy*160+xx] = wire_color(color);
}
static void text(uint16_t *pixels, int x, int y, const char *s, int scale, uint16_t color) {
    for (; *s && x<155; s++, x+=6*scale) {
        char c=*s; if(c>='a' && c<='z') c-=32;
        if(c==' ') continue;
        const char *p=strchr(alphabet,c); if(!p) p=strchr(alphabet,'?');
        const uint8_t *g=glyphs[p-alphabet];
        for(int row=0; row<7; row++) for(int col=0; col<5; col++)
            if(g[row] & (1u<<(4-col))) rect(pixels,x+col*scale,y+row*scale,scale,scale,color);
    }
}

void display_render(uint16_t pixels[160*80], const display_view_t *v) {
    memset(pixels,0,160*80*2);
    rect(pixels,0,0,160,14,0xdac4); text(pixels,5,3,"Ember Link",1,0xffff);
    text(pixels,4,21,v->title,strlen(v->title)<=13?2:1,v->attention?0xfd20:0xffff);
    text(pixels,4,43,v->line1,1,0xffff); text(pixels,4,56,v->line2,1,0xbdd7);
    if(v->percent>=0) { rect(pixels,4,69,152,6,0x3186); rect(pixels,4,69,152*v->percent/100,6,0x07d0); }
}
