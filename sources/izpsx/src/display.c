#include "display.h"
#include "native-menu.h"
#include "keepawake.h"
struct acquire_info{uint32_t type,pad;uint64_t next,swapchain,timeout,semaphore,fence;uint32_t mask,pad2;};
struct submit_info{uint32_t type,pad;uint64_t next;uint32_t waits,pad2;uint64_t wait,stage;uint32_t commands,pad3;uint64_t command;uint32_t signals,pad4;uint64_t signal;};
struct present_info{uint32_t type,pad;uint64_t next;uint32_t waits,pad2;uint64_t wait;uint32_t count,pad3;uint64_t swapchains,indices,results;};
_Static_assert(sizeof(struct acquire_info)==56,"Vulkan acquire ABI");
_Static_assert(sizeof(struct submit_info)==72,"Vulkan submit ABI");
_Static_assert(sizeof(struct present_info)==64,"Vulkan present ABI");
struct pad_state{uint64_t sample;uint32_t buttons,pad;int32_t lx,ly,rx,ry;uint32_t connected,reserved[7];};
static uint32_t previous_buttons;
static uint32_t menu_requested,raw_buttons;
int ps1_display_take_menu_request(void){int result=menu_requested!=0;menu_requested=0;return result;}
/* Exact floor(frames * hz * 1e6 / fps), modulo the native tick counter.
 * Dividing first avoids the old intermediate overflow after ~2h44m at
 * 31.25MHz/60Hz. The two remainder factors are each below 61e6. */
static uint64_t frame_ticks(uint64_t frames,uint64_t hz,uint64_t fps){
 uint64_t scale=hz*1000000ull,q=scale/fps,r=scale%fps;
 return frames*q+(frames/fps)*r+((frames%fps)*r)/fps;
}
void ps1_display_pace(uint64_t completed_frames){
 HeavyContext*b=&ps1_context->base;Ps1Display*d=(void*)(uintptr_t)b->display;
 uint64_t target=b->run_start+frame_ticks(completed_frames,b->tick_hz,ps1_context->reserved);
 uint64_t now=((uint64_t(*)(void))(uintptr_t)b->tick)(),ahead=target-now;
 /* A deadline less than half a timer cycle ahead remains valid at wrap. */
 if(ahead&&ahead<=(uint64_t)INT64_MAX){
  uint64_t seconds=ahead/b->tick_hz,remainder=ahead%b->tick_hz;
  uint64_t fraction=remainder*1000000000ull/b->tick_hz;
  uint64_t ns=seconds>(UINT64_MAX-fraction)/1000000000ull?UINT64_MAX:seconds*1000000000ull+fraction;
  ((void(*)(uint64_t))(uintptr_t)d->fn_sleep)(ns);
 }
}
uint32_t ps1_display_input(void){
 Ps1Display*d=(void*)(uintptr_t)ps1_context->base.display;uint32_t buttons=0;int32_t lx=0,ly=0;
 for(unsigned i=0;i<5;i++)if(d->pad[i]){struct pad_state s={0};uint32_t id=i?0:32;((void(*)(void*,const uint32_t*))(uintptr_t)d->pad[i])(&s,&id);if(s.connected&1){buttons|=s.buttons;if(!lx&&!ly){lx=s.lx;ly=s.ly;}}}
 d->observed_buttons|=buttons;d->input_samples++;
 raw_buttons=buttons;if((buttons&0x800)&&!(previous_buttons&0x800))menu_requested=1;
 previous_buttons=buttons;
 const uint32_t native[]={2,8,0x10,0x400,0x2000,0x8000,0x1000,0x4000,1,4,0x40,0x80,0x100,0x200};uint32_t out=0;for(unsigned i=0;i<14;i++)if(buttons&native[i])out|=1u<<i;
 if(lx<-12000)out|=1<<6;if(lx>12000)out|=1<<7;if(ly<-12000)out|=1<<5;if(ly>12000)out|=1<<4;return out;
}
uint32_t ps1_display_menu_buttons(void){
 uint32_t bits=ps1_display_input(),out=0;
 if(bits&(1u<<8))out|=PS1_MENU_A;if(bits&1u)out|=PS1_MENU_B;
 if(bits&(1u<<4))out|=PS1_MENU_UP;if(bits&(1u<<5))out|=PS1_MENU_DOWN;
 if(bits&(1u<<6))out|=PS1_MENU_LEFT;if(bits&(1u<<7))out|=PS1_MENU_RIGHT;
 if(raw_buttons&0x800)out|=PS1_PAUSE_MENU_BUTTON;
 if(ps1_context->base.stop)out|=PS1_MENU_EXIT_BUTTON;return out;
}
int ps1_display_present(void){
 Ps1Context*c=ps1_context;Ps1Display*d=(void*)(uintptr_t)c->base.display;if(!d||!d->image_count||d->image_count>8||!c->width||!c->height)return 1;
 volatile uint32_t*header=(void*)(uintptr_t)(c->base.frame-4096);header[0]=c->width;header[1]=c->height;header[2]=c->video_calls;__atomic_thread_fence(__ATOMIC_RELEASE);
 uint32_t index=0,stage=0x400;struct acquire_info a={1000060010,0,0,d->swapchain,100000000,d->acquire,0,1,0};int32_t r=((int32_t(*)(uint64_t,const void*,uint32_t*))(uintptr_t)d->fn_acquire)(d->device,&a,&index);if(r||index>=d->image_count){d->error=r?(uint32_t)r:1;return 2;}
 uint64_t signal=d->semaphores[index],command=d->commands[index];struct submit_info s={4,0,0,1,0,(uintptr_t)&d->acquire,(uintptr_t)&stage,1,0,(uintptr_t)&command,1,0,(uintptr_t)&signal};r=((int32_t(*)(uint64_t,uint32_t,const void*,uint64_t))(uintptr_t)d->fn_submit)(d->queue,1,&s,0);if(r){d->error=(uint32_t)r;return 3;}
 struct present_info pr={1000001001,0,0,1,0,(uintptr_t)&signal,1,0,(uintptr_t)&d->swapchain,(uintptr_t)&index,0};r=((int32_t(*)(uint64_t,const void*))(uintptr_t)d->fn_present)(d->queue,&pr);int32_t idle=((int32_t(*)(uint64_t))(uintptr_t)d->fn_idle)(d->queue);if(r||idle){d->error=(uint32_t)(r?r:idle);return 4;}d->submissions++;
 if(d->reserved){Ps1KeepAwake*k=(void*)(uintptr_t)d->reserved;if(k->enabled)ps1_keepawake_step(k,((uint64_t(*)(void))(uintptr_t)c->base.tick)());}
 return 0;
}
