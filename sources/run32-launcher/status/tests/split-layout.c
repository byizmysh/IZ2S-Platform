#include "../../native/split-layout.h"
#include <assert.h>
#include <stdio.h>

static void touch(uint8_t raw[656], float x, float y)
{
    memset(raw, 0, 656);
    split_touch_put(raw, 8, 1);
    split_touch_put(raw, 28, 9);
    split_touch_put(raw, 32, (uint32_t)(x / 1.5f));
    split_touch_put(raw, 36, (uint32_t)(y / 1.5f));
}
static void release(IzSplitUi *ui, IzSplitWindows *windows, unsigned *sound)
{ const uint8_t empty[656]={0}; split_ui_touch(ui, windows, sound, empty); }
int main(void)
{
    IzSplitUi ui={.corner=3,.size=2};
    IzSplitWindows windows={.visible=SPLIT_BOTH,.focus=SPLIT_RIGHT};
    unsigned sound=2; uint8_t raw[656], mapped[656];
    IzSplitLayout l=split_layout(&ui,windows.visible);
    assert(l.frame[0].x==4&&l.frame[1].x-(l.frame[0].x+l.frame[0].w)==4);
    assert(l.frame[0].h==l.content[0].h&&l.frame[1].h==l.content[1].h);
    float x=l.content[0].x+l.content[0].w/2, y=l.content[0].y+l.content[0].h/2;
    touch(raw,x,y);split_ui_touch(&ui,&windows,&sound,raw);
    assert(windows.focus==1&&sound==2&&ui.consumed);
    split_route_touch(&ui,&l,3,1,raw,mapped,false);assert(split_touch_word(mapped,8)==0);
    release(&ui,&windows,&sound);split_ui_touch(&ui,&windows,&sound,raw);
    split_route_touch(&ui,&l,3,1,raw,mapped,false);
    assert(split_touch_word(mapped,8)==1);
    assert(split_touch_word(mapped,32)>=636&&split_touch_word(mapped,32)<=640);
    assert(split_touch_word(mapped,36)>=356&&split_touch_word(mapped,36)<=360);
    split_route_touch(&ui,&l,3,2,raw,mapped,false);assert(split_touch_word(mapped,8)==0);
    split_route_touch(&ui,&l,3,1,raw,mapped,true);assert(split_touch_word(mapped,8)==0);
    release(&ui,&windows,&sound);
    ui.menu=true;l=split_layout(&ui,3);
    assert(l.frame[0].h-l.content[0].h==34&&l.frame[1].h-l.content[1].h==34);
    touch(raw,l.frame[1].x+l.frame[1].w-22,l.frame[1].y+14);split_ui_touch(&ui,&windows,&sound,raw);
    assert(sound==0&&windows.focus==1);release(&ui,&windows,&sound);
    split_ui_action(&ui,&windows,&sound,SPLIT_UI_SWAP);l=split_layout(&ui,3);
    assert(l.frame[1].x==4&&l.frame[0].x==1278);
    split_ui_action(&ui,&windows,&sound,SPLIT_UI_FLOAT);assert(ui.floating);
    for(unsigned corner=0;corner<4;corner++)for(unsigned size=0;size<=4;size++) {
        ui.corner=corner;ui.size=size;l=split_layout(&ui,3);
        IzSplitRect p=l.frame[ui.pip_slot];
        float area=p.w*p.h/(1920*1080.0f);assert(area>=0.125f&&area<=0.251f);
        assert(p.x>=4&&p.y>=4&&p.x+p.w<=1916&&p.y+p.h<=SPLIT_MENU_WORK_H-4);
        x=p.x+p.w/2;y=p.y+p.h/2;
        assert(split_hit_pane(&ui,&l,3,x,y)==1);
        ui.touch_pane=2;ui.consumed=false;touch(raw,x,y);
        split_route_touch(&ui,&l,3,2,raw,mapped,false);assert(split_touch_word(mapped,8)==0);
    }
    for(unsigned target=0;target<4;target++) {
        l=split_layout(&ui,3);IzSplitRect p=l.frame[ui.pip_slot];
        release(&ui,&windows,&sound);
        touch(raw,p.x+p.w/2,p.y+12);split_ui_touch(&ui,&windows,&sound,raw);
        assert(ui.dragging&&ui.consumed);
        touch(raw,(target&1)?1910:10,(target&2)?980:10);split_ui_touch(&ui,&windows,&sound,raw);
        release(&ui,&windows,&sound);assert(ui.corner==target&&!ui.dragging);
    }
    for(unsigned n=0;n<10;n++)split_ui_action(&ui,&windows,&sound,SPLIT_UI_SMALLER);
    assert(ui.size==0);
    for(unsigned n=0;n<10;n++)split_ui_action(&ui,&windows,&sound,SPLIT_UI_LARGER);
    assert(ui.size==4);
    split_ui_action(&ui,&windows,&sound,SPLIT_UI_SWAP);assert(ui.pip_slot==1);
    ui.menu=false;l=split_layout(&ui,3);
    assert(l.frame[0].h==1072);
    split_window_exit(&windows,1);l=split_layout(&ui,windows.visible);
    assert(!l.frame[0].w&&l.frame[1].w==1912&&windows.focus==2);
    assert(split_hit_pane(&ui,&l,windows.visible,1000,500)==2);
    puts("PASS: compact layout, independent focus/audio, touch mapping, PiP occlusion, four-corner drag, five sizes and survivor expansion");
}
