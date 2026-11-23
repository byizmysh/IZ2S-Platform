#include "ps2_app_menu.h"
#include <stdint.h>
#include <string.h>

static uint32_t packed_view;
void ps2_app_menu_publish(const Ps2AppMenuView *v) {
    uint32_t p=0;
    if(v) p=(!!v->visible)|((v->selected&3u)<<1)|((!!v->waiting)<<3)|
            ((!!v->confirming)<<4)|((v->message&7u)<<5);
    __atomic_store_n(&packed_view,p,__ATOMIC_RELEASE);
}
void ps2_app_menu_snapshot(Ps2AppMenuView *v) {
    if(!v)return;
    const uint32_t p=__atomic_load_n(&packed_view,__ATOMIC_ACQUIRE);
    *v=(Ps2AppMenuView){p&1u,(p>>1)&3u,(p>>3)&1u,(p>>4)&1u,(p>>5)&7u};
}

/* Compact original 5x7 labels; no texture, filesystem or platform dependency. */
static const uint8_t letters[26][7]={
 {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},
 {14,17,16,16,16,17,14},{30,17,17,17,17,17,30},
 {31,16,16,30,16,16,31},{31,16,16,30,16,16,16},
 {14,17,16,23,17,17,15},{17,17,17,31,17,17,17},
 {14,4,4,4,4,4,14},{7,2,2,2,18,18,12},
 {17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
 {17,27,21,21,17,17,17},{17,25,21,19,17,17,17},
 {14,17,17,17,17,17,14},{30,17,17,30,16,16,16},
 {14,17,17,17,21,18,13},{30,17,17,30,20,18,17},
 {15,16,16,14,1,1,30},{31,4,4,4,4,4,4},
 {17,17,17,17,17,17,14},{17,17,17,17,17,10,4},
 {17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
 {17,17,10,4,4,4,4},{31,1,2,4,8,16,31}
};
static const uint8_t two[7]={14,17,1,2,4,8,31};
static const uint8_t three[7]={30,1,1,14,1,1,30};
static void box(Ps2SwMergeOutput *o,int x,int y,int w,int h,uint32_t rgb) {
    int l=x<0?0:x,t=y<0?0:y,r=x+w,b=y+h;
    if(r>(int)o->width)r=(int)o->width;
    if(b>(int)o->height)b=(int)o->height;
    for(int py=t;py<b;py++)for(int px=l;px<r;px++){
        uint8_t *p=o->pixels+(size_t)py*o->pitch+(size_t)px*4;
        p[0]=(uint8_t)(rgb>>16);p[1]=(uint8_t)(rgb>>8);p[2]=(uint8_t)rgb;p[3]=255;
    }
}
static void text(Ps2SwMergeOutput *o,int x,int y,const char *s,int scale,uint32_t rgb) {
    for(;*s;s++,x+=6*scale){
        const uint8_t *glyph=(*s>='A'&&*s<='Z')?letters[*s-'A']:(*s=='2'?two:(*s=='3'?three:NULL));
        if(!glyph)continue;
        for(int row=0;row<7;row++)for(int col=0;col<5;col++)
            if(glyph[row]&(1u<<(4-col)))box(o,x+col*scale,y+row*scale,scale,scale,rgb);
    }
}
int ps2_app_menu_render(Ps2SwMergeOutput *o,const Ps2AppMenuView *v) {
    if(!o||!v||!o->pixels||!o->width||!o->height||o->width>1024||o->height>1024||
       o->pitch<(size_t)o->width*4||
       (o->height-1u)>(SIZE_MAX-(size_t)o->width*4)/o->pitch||
       o->bytes<(size_t)(o->height-1u)*o->pitch+(size_t)o->width*4||
       o->bytes>UINTPTR_MAX-(uintptr_t)o->pixels)return 1;
    if(!v->visible)return 0;
    const int scale=o->width>=480&&o->height>=320?2:1;
    const int w=31*6*scale,h=15*10*scale;
    const int x=((int)o->width-w)/2,y=((int)o->height-h)/2;
    box(o,x-3,y-3,w+6,h+6,0x91bde8);box(o,x,y,w,h,0x142538);
    text(o,x+2*scale,y+3*scale,"PS2 PAUSED",scale,0xffffff);
    static const char *items[]={"RESUME","SAVE STATE","LOAD STATE","SAVE AND EXIT"};
    for(unsigned i=0;i<4;i++){
        const int row=y+(18+(int)i*15)*scale;
        if(i==(v->selected&3u))box(o,x+scale,row-2*scale,w-2*scale,12*scale,0x315a80);
        text(o,x+8*scale,row,items[i],scale,0xffffff);
    }
    const char *status=v->message==PS2_MENU_OUTCOME_UNKNOWN?"ACTION UNCONFIRMED":
        v->waiting?"PLEASE WAIT":v->confirming?"PRESS CROSS TO CONFIRM":
        v->message==PS2_MENU_SAVED?"STATE SAVED":v->message==PS2_MENU_LOADED?"STATE LOADED":
        v->message==PS2_MENU_FAILED?"ACTION FAILED":"UP DOWN SELECT   CROSS OPEN";
    text(o,x+3*scale,y+87*scale,status,scale,0xffd591);
    text(o,x+3*scale,y+104*scale,"CIRCLE RESUME",scale,0xbdcede);
    text(o,x+3*scale,y+124*scale,"DOUBLE R3 SAVE AND EXIT",scale,0xbdcede);
    text(o,x+3*scale,y+137*scale,"L3 R3 HOLD PAUSES",scale,0xbdcede);
    return 0;
}
