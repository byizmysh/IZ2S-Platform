#define _GNU_SOURCE
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#ifndef PS1_IMPORT_COUNT
#define PS1_IMPORT_COUNT 1
#endif
#define typeof __typeof__
#include "../src/frontend.c"
#include <assert.h>
Ps1Context*ps1_context;
int main(void){
 Ps1Context context={0};ps1_context=&context;live_buttons[0]=0x0041;live_buttons[1]=0x1080;
 assert((uint16_t)input(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_MASK)==0x0041);
 assert((uint16_t)input(1,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_MASK)==0x1080);
 assert(input(0,RETRO_DEVICE_JOYPAD,0,0)==1&&input(1,RETRO_DEVICE_JOYPAD,0,0)==0);
 assert(input(1,RETRO_DEVICE_JOYPAD,0,7)==1&&input(0,RETRO_DEVICE_JOYPAD,0,7)==0);
 live_buttons[2]=2;live_buttons[3]=4;assert(input(2,RETRO_DEVICE_JOYPAD,0,1)==1&&input(3,RETRO_DEVICE_JOYPAD,0,2)==1);assert(input(4,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_MASK)==0);
 assert(input(1,RETRO_DEVICE_ANALOG,0,0)==0);
 uint32_t replay[]={2,4};context.input=(uintptr_t)replay;context.base.frames_done=1;
 assert(input(0,RETRO_DEVICE_JOYPAD,0,2)==1&&input(1,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_MASK)==0);
 puts("PASS: production libretro callback routes independent P1/P2/P3/P4 masks; bit and mask queries; invalid ports; legacy replay isolation");
}
