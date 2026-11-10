#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/mman.h>
#include "controllers.h"
#include "settings.h"
#include "hid-bindings.h"
static uint32_t connected[9]={[0]=1,[1]=1,[8]=1},set_calls,reject,overrun;
static uint64_t buttons[9]={[0]=1,[1]=2,[8]=4};static int32_t axes[9][4]={[0]={19000,0,0,0},[1]={0,-20000,0,0},[8]={-25000,0,0,0}};
static uint32_t set_ids(const uint32_t*ids,int32_t n){assert(n==9&&ids[0]==0&&ids[1]==1&&ids[7]==7&&ids[8]==32);set_calls++;return reject;}
static void pad(void*out,const uint32_t*id){
 unsigned n=*id==32?8:*id;assert(n<9);
 if(connected[n]){uint32_t attributes=1;memcpy((char*)out+8,buttons+n,8);memcpy((char*)out+16,axes[n],16);memcpy((char*)out+32,&attributes,4);}
 if(overrun)((unsigned char*)out)[64]=0;
}
static unsigned styles[9]={[0]=PS1_PAD_FULL,[1]=PS1_PAD_FULL,[2]=PS1_PAD_FULL,[3]=PS1_PAD_FULL,[4]=PS1_PAD_FULL,[5]=PS1_PAD_FULL,[6]=PS1_PAD_FULL,[7]=PS1_PAD_FULL,[8]=PS1_PAD_HANDHELD};
static void full(void*out,const uint32_t*id){if(styles[*id]==PS1_PAD_FULL)pad(out,id);}
static void handheld(void*out,const uint32_t*id){assert(*id==32);pad(out,id);}
static void dual(void*out,const uint32_t*id){if(styles[*id]==PS1_PAD_DUAL)pad(out,id);}
static void left(void*out,const uint32_t*id){if(styles[*id]==PS1_PAD_LEFT)pad(out,id);}
static void right(void*out,const uint32_t*id){if(styles[*id]==PS1_PAD_RIGHT)pad(out,id);}
static uint32_t supported_styles=7,styles_reject;
static uint32_t set_styles(uint32_t mask){if(styles_reject)return styles_reject;supported_styles=mask;return 0;}
static uint32_t get_styles(uint32_t*out){*out=supported_styles;return 0;}
static uint32_t disconnected_id=99,disconnect_reject;
static uint32_t disconnect_pad(const uint32_t*id){disconnected_id=*id;if(disconnect_reject)return disconnect_reject;connected[*id]=0;return 0;}
static void setup(Ps1Controllers*p){memset(p,0,sizeof *p);p->functions[PS1_HID_setIds]=(uintptr_t)set_ids;p->functions[PS1_HID_padFullKey]=(uintptr_t)full;p->functions[PS1_HID_padHandheld]=(uintptr_t)handheld;p->functions[PS1_HID_padJoyDual]=(uintptr_t)dual;p->functions[PS1_HID_padJoyLeft]=(uintptr_t)left;p->functions[PS1_HID_padJoyRight]=(uintptr_t)right;p->functions[PS1_HID_disconnect]=(uintptr_t)disconnect_pad;p->functions[PS1_HID_setStyles]=(uintptr_t)set_styles;p->functions[PS1_HID_getStyles]=(uintptr_t)get_styles;}
int main(void){
 size_t n=6164480;unsigned char*sdk=mmap(0,n,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_32BIT,-1,0);assert(sdk!=MAP_FAILED);
 for(unsigned i=0;i<PS1_HID_COUNT;i++)memcpy(sdk+ps1_hid_bindings[i].offset,ps1_hid_bindings[i].bytes,64);
 Ps1Controllers p;assert(!ps1_controllers_bind(&p,(uintptr_t)sdk,n));assert(ps1_controllers_bind(&p,(uintptr_t)sdk,1));sdk[ps1_hid_bindings[2].offset]^=1;assert(ps1_controllers_bind(&p,(uintptr_t)sdk,n));sdk[ps1_hid_bindings[2].offset]^=1;
 setup(&p);assert(!ps1_controllers_enable(&p)&&set_calls==1);assert(!ps1_controllers_poll(&p));assert(p.player[0].buttons==1&&p.player[1].buttons==2&&p.player[0].axes[0]==19000&&p.player[1].axes[1]==-20000);
 connected[0]=0;connected[8]=0;assert(!ps1_controllers_poll(&p));assert(!p.player[0].connected&&!p.player[0].buttons&&!p.player[0].axes[0]&&p.player[1].buttons==2);
 connected[8]=1;assert(!ps1_controllers_poll(&p));assert(p.player[0].id==32&&p.player[0].buttons==4&&p.player[1].buttons==2);
 connected[0]=1;connected[1]=0;assert(!ps1_controllers_poll(&p));assert(p.player[0].id==0&&p.player[0].buttons==1&&!p.player[1].connected&&!p.player[1].buttons&&!p.player[1].axes[1]);
 connected[1]=1;buttons[1]=8;assert(!ps1_controllers_poll(&p)&&p.player[1].buttons==8);
 reject=0x1234;assert(ps1_controllers_enable(&p)==0x1234);assert(!ps1_controllers_poll(&p)&&p.player[0].connected&&!p.player[1].connected);
 reject=0;assert(!ps1_controllers_enable(&p));assert(!ps1_controllers_poll(&p));
 uint32_t assignment[PS1_PLAYER_COUNT]={PS1_DEVICE_OFF,PS1_DEVICE_AUTO};ps1_controllers_assign(&p,assignment);assert(!p.player[0].connected&&p.menu.connected&&p.menu.buttons==buttons[0]&&p.player[1].connected);
 connected[2]=1;buttons[2]=0x800;assignment[0]=PS1_DEVICE_PAD1+2;ps1_controllers_assign(&p,assignment);assert(!ps1_controllers_poll(&p)&&p.player[0].id==2&&p.player[0].buttons==0x800&&p.player[1].id==1);
 disconnect_reject=0xdead;assert(ps1_controllers_disconnect(&p,1)==0xdead&&connected[1]);disconnect_reject=0;assert(!ps1_controllers_disconnect(&p,1)&&disconnected_id==1);assert(!ps1_controllers_poll(&p)&&!p.player[1].connected&&p.player[0].connected);
 assignment[1]=PS1_DEVICE_OFF;ps1_controllers_assign(&p,assignment);assert(ps1_controllers_disconnect(&p,1)==0xf332);
 connected[2]=connected[3]=1;buttons[2]=4;buttons[3]=8;assignment[0]=assignment[1]=PS1_DEVICE_AUTO;ps1_controllers_assign(&p,assignment);assert(!ps1_controllers_poll(&p));assert(p.player[2].buttons==4&&p.player[3].buttons==8);
 assignment[2]=PS1_DEVICE_OFF;ps1_controllers_assign(&p,assignment);assert(!p.player[2].connected&&p.player[3].buttons==8);assignment[2]=PS1_DEVICE_AUTO;
 styles[0]=PS1_PAD_LEFT;buttons[0]=0x8000;axes[0][0]=0;axes[0][1]=20000;assert(!ps1_controllers_poll(&p));assert(p.player[0].style==PS1_PAD_LEFT&&p.player[0].buttons==1&&p.player[0].axes[0]==-20000&&ps1_controller_menu_buttons(&p.player[0])==0x1001);
 buttons[0]=0x1000|0x800;assert(!ps1_controllers_poll(&p)&&p.player[0].buttons==(2|0x400));buttons[0]=0x800|0x30000;assert(!ps1_controllers_poll(&p)&&p.player[0].buttons==0x800);
 buttons[0]=0x30000;assert(!ps1_controllers_poll(&p)&&!p.player[0].buttons&&p.player[0].shortcut_latched);buttons[0]=0;assert(!ps1_controllers_poll(&p)&&!p.player[0].shortcut_latched);buttons[0]=0x30000;assert(!ps1_controllers_poll(&p)&&p.player[0].buttons==0xc0);
 styles[0]=PS1_PAD_RIGHT;buttons[0]=1|0x20;axes[0][2]=19000;axes[0][3]=-21000;assert(!ps1_controllers_poll(&p));assert(p.player[0].buttons==(2|0x10)&&p.player[0].axes[0]==-21000&&p.player[0].axes[1]==-19000);
 buttons[0]=0x400|0xc0000;assert(!ps1_controllers_poll(&p)&&p.player[0].buttons==0x800);
 styles[0]=PS1_PAD_DUAL;buttons[0]=0x8000|1;assert(!ps1_controllers_poll(&p)&&p.player[0].buttons==(0x8000|1)&&p.player[0].style==PS1_PAD_DUAL);
 assert(supported_styles==31&&p.restore_styles);styles_reject=0x123;assert(ps1_controllers_restore(&p)==0x123&&p.restore_styles);styles_reject=0;assert(!ps1_controllers_restore(&p)&&supported_styles==7&&!p.restore_styles);
 styles_reject=0x234;assert(ps1_controllers_enable(&p)==0x234&&p.configured&&p.style_error==0x234);styles_reject=0;assert(!ps1_controllers_enable(&p));
 overrun=1;assert(ps1_controllers_poll(&p)==0xf331&&!p.player[0].buttons&&!p.player[1].buttons);
 assert(ps1_controller_buttons(2)==1&&ps1_controller_buttons(1)==256&&ps1_controller_buttons(0x400)==8&&ps1_controller_buttons(0x800)==0&&ps1_controller_buttons(32)==0);
 munmap(sdk,n);puts("PASS: exact SDK binding; four independent pads and sticks; sideways Joy-Con halves; style restoration; stable slots; neutral disconnect; handheld fallback; reconnect; configuration rejection and overrun guard");
}
