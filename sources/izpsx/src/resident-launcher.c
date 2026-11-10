#include "resident-launcher.h"
#include <stddef.h>
#define EXT_FLAGS 0x7f000cu
#define STREAM_CLOCK (0x700c80u+240u)
static void copy(void*destination,const void*source,uint64_t bytes){volatile uint8_t*d=destination;const volatile uint8_t*s=source;while(bytes--)*d++=*s++;}
static void zero(void*destination,uint64_t bytes){volatile uint8_t*d=destination;while(bytes--)*d++=0;}
static volatile uint32_t*word(ResidentLauncher*c,uint64_t offset){return (void*)(uintptr_t)(c->storage+offset);}
static void restore_pad_flags(ResidentLauncher*c){if(c->extension_flags_address){volatile uint32_t*p=(void*)(uintptr_t)c->extension_flags_address;*p=(*p&~64u)|(c->extension_flags_saved&64u);c->extension_flags_address=0;}}
static uint32_t fail(ResidentLauncher*c,uint32_t error){if(c){restore_pad_flags(c);c->status=RESIDENT_FAILED;c->error=error;c->errors++;}return RESIDENT_ERROR;}
static void clear_request(ResidentLauncher*c){*word(c,1012)=0;*word(c,1016)=0;zero((void*)(uintptr_t)(c->storage+960),16);}
uint32_t resident_launcher_enter(ResidentLauncher*c){
 if(!c)return RESIDENT_ERROR;
 if(c->magic!=RESIDENT_LAUNCHER_MAGIC||c->version!=1||!c->snapshot||!c->storage||c->storage_bytes!=RESIDENT_MENU_BYTES||c->storage_capacity<c->storage_bytes||
    ((c->storage|c->snapshot)&7)||c->snapshot+c->storage_bytes<c->snapshot||c->storage+c->storage_capacity<c->storage||
    (c->snapshot<c->storage+c->storage_bytes&&c->storage<c->snapshot+c->storage_bytes)||c->ps1_slot>=15||!c->render||!c->tick||!c->sleep||c->tick_hz<60||c->tick_hz>1000000000ull){c->status=RESIDENT_FAILED;c->error=1;c->errors++;return RESIDENT_ERROR;}
 restore_pad_flags(c);c->status=RESIDENT_RESTORING;c->error=0;
 copy((void*)(uintptr_t)c->storage,(void*)(uintptr_t)c->snapshot,c->storage_bytes);c->restored_bytes+=c->storage_bytes;c->entries++;
 clear_request(c);*word(c,0x700004)=0;c->input_armed=0;c->period_remainder=0;
 /* Current GPU normalisation otherwise overwrites native input with stale
  * host-owned source mailboxes. Preserve every other extension feature bit. */
 if(c->storage_capacity>=EXT_FLAGS+4&&*word(c,0x7f0000)==0x48425344u&&*word(c,0x7f0004)==1){c->extension_flags_address=c->storage+EXT_FLAGS;c->extension_flags_saved=*word(c,EXT_FLAGS);*word(c,EXT_FLAGS)&=~64u;}
 c->deadline=c->last_tick=((uint64_t(*)(void))(uintptr_t)c->tick)();
 *word(c,STREAM_CLOCK+8)=(uint32_t)c->tick_hz;__atomic_thread_fence(__ATOMIC_RELEASE);c->status=RESIDENT_MENU;return RESIDENT_CONTINUE;
}
static int32_t clamp(int32_t value){return value<-32767?-32767:value>32767?32767:value;}
static uint32_t magnitude(int32_t value){return value<0?(uint32_t)(-(int64_t)value):(uint32_t)value;}
static void merge_axis(int32_t*destination,int32_t value){value=clamp(value);if(magnitude(value)>magnitude(*destination))*destination=value;}
uint32_t resident_launcher_frame(ResidentLauncher*c){
 if(!c||c->status!=RESIDENT_MENU)return RESIDENT_ERROR;
 if(c->stop_request){restore_pad_flags(c);c->status=RESIDENT_STOPPED;return RESIDENT_STOP;}
 ResidentPad aggregate;ResidentTouch touch;zero(&aggregate,sizeof aggregate);zero(&touch,sizeof touch);
 for(unsigned i=0;i<5;i++)if(c->pad[i]){
  ResidentPad sample;zero(&sample,sizeof sample);uint32_t id=i?0:32;((void(*)(ResidentPad*,const uint32_t*))(uintptr_t)c->pad[i])(&sample,&id);c->pad_samples++;
  if(!(sample.attributes&1))continue;
  if(sample.sample>aggregate.sample)aggregate.sample=sample.sample;
  aggregate.buttons|=sample.buttons;aggregate.upper|=sample.upper;aggregate.attributes|=sample.attributes;
  merge_axis(&aggregate.lx,sample.lx);merge_axis(&aggregate.ly,sample.ly);merge_axis(&aggregate.rx,sample.rx);merge_axis(&aggregate.ry,sample.ry);
 }
 if(c->touch){((void(*)(ResidentTouch*))(uintptr_t)c->touch)(&touch);c->touch_samples++;if(touch.count>16)touch.count=16;}
 c->observed_buttons|=aggregate.buttons;
 if(!c->input_armed){if(!aggregate.buttons&&!aggregate.upper&&!touch.count)c->input_armed=1;else {aggregate.buttons=aggregate.upper=0;aggregate.lx=aggregate.ly=aggregate.rx=aggregate.ry=0;zero(&touch,sizeof touch);}}
 copy((void*)(uintptr_t)c->storage,&aggregate,sizeof aggregate);copy((void*)(uintptr_t)(c->storage+128),&touch,sizeof touch);
 uint64_t now=((uint64_t(*)(void))(uintptr_t)c->tick)();if(now<c->last_tick)return fail(c,2);c->last_tick=now;
 *word(c,STREAM_CLOCK)=(uint32_t)now;*word(c,STREAM_CLOCK+4)=(uint32_t)(now>>32);__atomic_thread_fence(__ATOMIC_RELEASE);
 uint32_t rendered=((uint32_t(*)(void*))(uintptr_t)c->render)((void*)(uintptr_t)c->render_argument);if(rendered)return fail(c,0x10000u|(rendered&65535u));
 __atomic_thread_fence(__ATOMIC_ACQUIRE);c->frames++;
 uint32_t request=*word(c,1012);c->last_request=request;
 if(request){clear_request(c);if(request==c->ps1_slot+1){restore_pad_flags(c);c->status=RESIDENT_SELECTED;return RESIDENT_SELECT_PS1;}*word(c,92)=1;c->rejected++;}
 /* Return requests belong to the emulated application, not this native pump. */
 *word(c,1016)=0;
 now=((uint64_t(*)(void))(uintptr_t)c->tick)();if(now<c->last_tick)return fail(c,2);c->last_tick=now;
 c->deadline+=c->tick_hz/60;c->period_remainder+=(uint32_t)(c->tick_hz%60);if(c->period_remainder>=60){c->deadline++;c->period_remainder-=60;}
 if(now<c->deadline)((void(*)(uint64_t))(uintptr_t)c->sleep)((c->deadline-now)*1000000000ull/c->tick_hz);
 else if(now>c->deadline){c->late_frames++;c->deadline=now;}
 return RESIDENT_CONTINUE;
}
uint32_t resident_launcher_run(ResidentLauncher*c){uint32_t result=resident_launcher_enter(c);while(result==RESIDENT_CONTINUE)result=resident_launcher_frame(c);return result;}
