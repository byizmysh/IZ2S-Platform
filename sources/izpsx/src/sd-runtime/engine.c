/* IZPSX native SD engine. Serialized events on the console supervisor; no
 * network, host service, per-frame allocation or helper thread. */
#include "services.h"
#include "address.h"
#include "bridge.h"
#include "platform.h"
#include "audio.h"
#include "memory-card.h"
#include "native-menu.h"
#include "settings-menu.h"
#include "fs-file.h"
#include "sd-layout.h"
#include "replay.h"
#include "sd-imports.h"
#include "controllers.h"
#include <string.h>
uint32_t ps1_sd_core_begin(Ps1Context*);
uint32_t ps1_sd_core_frame4(uint32_t,uint32_t,uint32_t,uint32_t);
uint32_t ps1_sd_core_end(void);
uint32_t ps1_sd_core_reset(void);
void ps1_sd_core_ports(uint32_t connected_mask);
uint32_t ps1_sd_jit_probe(Ps1Context*);
#define JIT_BYTES 0x1001000u
#define HEAP_BYTES (64u*1024u*1024u)
#define STACK_BYTES (4u*1024u*1024u)
typedef struct {
 Ps1Context context;
 Ps1SdDirectory sd;
 Ps1FileIO io;
 Ps1Audio audio;
 uintptr_t imports[PS1_IMPORT_COUNT];
 Ps1Menu menu;Ps1Pause pause;Ps1SdEntry entries[128];
 Ps1Replay replay;
 Ps1Settings settings;Ps1SettingsMenu settings_menu;Ps1Preferences launched;
 Ps1FrameClock picture_clock;
 Ps1Controllers controllers;
 Ps1SdServices services,*owner;IzNativeApp4*app;
 uint64_t allocations[6],stack,previous,pause_start,rate_tick,rate_frames,rate_pictures,pictures;
 unsigned settings_return,picture_w,picture_h,emu_rate,picture_rate;
 uint32_t settings_request,capture_request,menu_source;
 unsigned phase,core_live,failed,uncertain,game_w,game_h,game_calls,jit_ready;
 char game[PS1_SD_PATH_MAX],log[8192];
} State;
_Static_assert(sizeof(State)+IZPSX_STATE_OFFSET<=IZPSX_APP_DATA_BYTES,"SD state allocation");
static uint32_t fault(State*s,uint32_t rc){s->failed=rc;s->phase=4;s->context.base.error=rc;if(s->app&&s->app->base.base.base.controls){volatile uint32_t*t=(void*)((uint8_t*)s->app->base.base.base.controls+784);t[1]=4;t[3]=rc;}return rc;}
static uint32_t library_error(State*s,uint32_t rc,unsigned kind){
 if(s->replay.frames){volatile uint32_t*q=(void*)((uint8_t*)s->app->base.base.base.controls+848);q[5]=3;q[6]=rc;__atomic_thread_fence(__ATOMIC_RELEASE);q[4]=s->replay.request;}
 s->replay.frames=0;s->menu.status=0;s->menu.error=rc;s->menu.error_kind=kind;s->phase=1;return 0;
}
static uint32_t allocate(State*s,unsigned slot,uint64_t bytes){
 uint64_t out=0;uint32_t rc=(uint32_t)CALL(s->owner,F_ALLOCATE,&out,4096,bytes,0,0,0,0,0);
 if((rc&&out)||(!rc&&(!out||(out&4095)||out>=(1ull<<39)||bytes>(1ull<<39)-out))){s->uncertain=1;return rc?rc:0xf201;}
 if(out)s->allocations[slot]=out;
 if(rc)return rc;
 return 0;
}
static uint32_t psbuttons(State*s){
 return ps1_controller_buttons(ps1_controller_menu_buttons(&s->controllers.menu));
}
static uint32_t menu_buttons(void*arg){State*s=arg;uint32_t b=psbuttons(s),out=0;
 if(b&(1u<<8))out|=PS1_MENU_A;if(b&1)out|=PS1_MENU_B;if(b&(1u<<4))out|=PS1_MENU_UP;if(b&(1u<<5))out|=PS1_MENU_DOWN;if(b&(1u<<6))out|=PS1_MENU_LEFT;if(b&(1u<<7))out|=PS1_MENU_RIGHT;
 uint32_t raw=(uint32_t)ps1_controller_menu_buttons(&s->controllers.menu);
 if(raw&0x800)out|=PS1_PAUSE_MENU_BUTTON;if(raw&0x40)out|=PS1_MENU_L;if(raw&0x80)out|=PS1_MENU_R;if(raw&4)out|=PS1_MENU_X;if(raw&8)out|=PS1_MENU_Y;if(raw&0x100)out|=PS1_MENU_ZL;if(raw&0x200)out|=PS1_MENU_ZR;return out;
}
static int unused_present(void*a,const uint32_t*b,unsigned w,unsigned h){(void)a;(void)b;(void)w;(void)h;return 0;}
static void unused_sleep(void*a,uint64_t ns){(void)a;(void)ns;}
static Ps1MenuConfig config(State*s){
 uint32_t mask=0;for(unsigned i=0;i<PS1_PLAYER_COUNT;i++)mask|=s->controllers.player[i].connected<<i;
 Ps1MenuConfig c={.sd=&s->sd,.root=IZPSX_SD_GAMES,.entries=s->entries,.capacity=128,.rgba=(uint32_t*)s->app->base.base.private_memory+1024,.user=s,.buttons=menu_buttons,.present=unused_present,.sleep=unused_sleep,.preferences=&s->settings.prefs,.launch_source=s->owner->ram_only?"Loaded from session memory (RAM)":"Loaded from SD card",.controller_status="LIVE",.controller_mask=mask,.controller_error=s->controllers.configuration_error,.single_joycon=s->controllers.menu.style==PS1_PAD_LEFT||s->controllers.menu.style==PS1_PAD_RIGHT};
 for(unsigned i=0;i<PS1_PLAYER_COUNT;i++)c.controller_style[i]=s->controllers.style_error?"Single Joy-Con unavailable - HID style configuration failed.":ps1_controller_style_name(s->controllers.player[i].style);
 return c;
}
static void apply_live(State*s){const uint32_t*v=s->settings.prefs.value;static const unsigned delays[]={0,500,1000,2000};ps1_audio_config(v[PS1_VOLUME]*25,v[PS1_MUTE],v[PS1_MONO],2+v[PS1_AUDIO_BUFFER]*2);ps1_cards_set_delay(delays[v[PS1_SAVE_DELAY]]);memset(&s->picture_clock,0,sizeof s->picture_clock);}
static void show_picture(State*s,uint64_t now,int force){
 unsigned limit=ps1_display_limit(&s->settings.prefs),native=(unsigned)((s->context.reserved+500000)/1000000),rate=limit&&limit<native?limit:native;
 if(!force&&!ps1_frame_clock_due(&s->picture_clock,now,s->app->base.base.base.tick_hz,rate))return;
 uint32_t*p=(uint32_t*)s->app->base.base.private_memory+1024;s->picture_w=s->context.width;s->picture_h=s->context.height;
 memcpy(p,(void*)(uintptr_t)s->context.base.frame,(size_t)s->picture_w*s->picture_h*4);s->pictures++;
 if(s->settings.prefs.value[PS1_SHOW_FPS])ps1_performance_draw(p,s->picture_w,s->picture_h,s->emu_rate,s->picture_rate);
}
static void publish(State*s){
 volatile uint32_t*h=s->app->base.base.private_memory;int game=(s->phase==2||s->phase==6)&&s->pictures;
 h[0]=game?s->picture_w:s->context.width;h[1]=game?s->picture_h:s->context.height;h[2]++;
 const uint32_t*v=s->settings.prefs.value;h[3]=game?v[PS1_ASPECT]:0;h[4]=game?v[PS1_FILTER]:0;h[5]=game?v[PS1_SCANLINES]:0;h[6]=game?80+v[PS1_BRIGHTNESS]*10:100;h[7]=game?v[PS1_CROP]*4:0;
 /* Shared controls outlive module data, so observers never follow a pointer
  * that can be freed when the player returns to the launcher. */
 volatile uint32_t*t=(void*)((uint8_t*)s->app->base.base.base.controls+784);t[15]++;__atomic_thread_fence(__ATOMIC_RELEASE);
 t[0]=0x58505349;t[1]=s->phase;t[2]=s->context.base.frames_done;t[3]=s->failed;t[4]=(uint32_t)ps1_cards_saved();t[5]=s->jit_ready;t[6]=s->context.width;t[7]=s->context.height;
 uint64_t now=CALL(s->owner,F_TICK,0,0,0,0,0,0,0,0);t[8]=(uint32_t)now;t[9]=(uint32_t)(now>>32);t[10]=(uint32_t)s->context.base.max_step_ticks;t[11]=(uint32_t)s->context.base.boot_start;t[12]=(uint32_t)(s->context.base.boot_start>>32);t[13]=(uint32_t)s->context.base.boot_end;t[14]=(uint32_t)(s->context.base.boot_end>>32);
 __atomic_thread_fence(__ATOMIC_RELEASE);t[15]++;
 if(s->app->base.base.base.controls_bytes>=65792){volatile uint32_t*d=(void*)((uint8_t*)s->app->base.base.base.controls+65536);d[0]=0x53465849;d[1]=s->phase;d[2]=(uint32_t)s->pictures;d[3]=ps1_display_limit(&s->settings.prefs);d[4]=s->emu_rate;d[5]=s->picture_rate;d[6]=(uint32_t)s->settings.generation;d[7]=s->settings.warning;for(unsigned i=0;i<PS1_OPTION_COUNT;i++)d[8+i]=v[i];d[36]=s->owner->ram_only?1:2;for(unsigned i=0;i<PS1_CONTROL_COUNT;i++)d[37+i]=s->settings.prefs.player2[i];d[41]=s->settings_menu.player;d[42]=s->settings.prefs.controller_device[0];d[43]=s->settings.prefs.controller_device[1];d[44]=s->settings.prefs.multitap;for(unsigned player=2;player<PS1_PLAYER_COUNT;player++){for(unsigned i=0;i<PS1_CONTROL_COUNT;i++)d[45+(player-2)*4+i]=s->settings.prefs.extra_player[player-2][i];d[53+player-2]=s->settings.prefs.controller_device[player];}}
 if(s->app->base.base.base.controls_bytes>=66432){volatile uint32_t*d=(void*)((uint8_t*)s->app->base.base.base.controls+66304);d[2]++;__atomic_thread_fence(__ATOMIC_RELEASE);d[0]=0x41505849;d[1]=1;d[3]=s->audio.opened|(s->audio.started<<1)|(s->audio.uncertain<<2);volatile uint64_t*q=(void*)(d+4);q[0]=s->audio.appends;q[1]=s->audio.releases;q[2]=s->audio.output_frames;q[3]=s->audio.underruns;q[4]=s->audio.pending;q[5]=s->audio.empty_episodes;q[6]=s->audio.peak_pending;__atomic_thread_fence(__ATOMIC_RELEASE);d[2]++;}
 if(s->app->base.base.base.controls_bytes>=66752){
  volatile uint32_t*d=(void*)((uint8_t*)s->app->base.base.base.controls+66432);d[2]++;__atomic_thread_fence(__ATOMIC_RELEASE);
  d[0]=0x43505849;d[1]=2;d[3]=s->controllers.configuration_error;d[4]=s->controllers.configured;d[5]=s->controllers.polls;
  for(unsigned player=0;player<PS1_PLAYER_COUNT;player++){const Ps1Controller*p=&s->controllers.player[player];unsigned at=6+player*12;d[at]=p->connected;d[at+1]=p->id;d[at+2]=(uint32_t)p->buttons;d[at+3]=(uint32_t)(p->buttons>>32);for(unsigned axis=0;axis<4;axis++)d[at+4+axis]=(uint32_t)p->axes[axis];d[at+8]=ps1_controller_buttons(p->buttons);d[at+9]=p->attributes;d[at+10]=(uint32_t)p->sample;d[at+11]=(uint32_t)(p->sample>>32);}
  d[54]=s->controllers.style_error;for(unsigned i=0;i<PS1_PLAYER_COUNT;i++){const Ps1Controller*p=&s->controllers.player[i];d[55+i]=p->style;d[59+i*2]=(uint32_t)p->physical_buttons;d[60+i*2]=(uint32_t)(p->physical_buttons>>32);}
  __atomic_thread_fence(__ATOMIC_RELEASE);d[2]++;
 }
 if(s->capture_request&&s->app->base.base.base.controls_bytes>=0x300000u+640u*480u*4u){
  uint8_t*shared=s->app->base.base.base.controls;const void*pixels=(uint32_t*)s->app->base.base.private_memory+1024;uint64_t hash=1469598103934665603ull;const uint8_t*b=pixels;for(unsigned i=0;i<640*480*4;i++){hash^=b[i];hash*=1099511628211ull;}
  memcpy(shared+0x300000,pixels,640*480*4);volatile uint32_t*meta=(void*)(shared+66080);meta[0]=0x55495849;meta[1]=640;meta[2]=480;meta[3]=640*480*4;*(volatile uint64_t*)(shared+66096)=hash;
  volatile uint32_t*q=(void*)(shared+66048);q[5]=2;q[6]=0;__atomic_thread_fence(__ATOMIC_RELEASE);q[4]=s->capture_request;s->capture_request=0;
 }
}
/* Explicit local qualification requests; no polling or remote service is
 * needed for play. Accept only in picker/paused UI, publish magic last, ack
 * once, and never stop a running game to apply a diagnostic request. */
static uint32_t qualify_settings(State*s){
 IzNativeApp*app=&s->app->base.base.base;if(app->controls_bytes<66112)return 0;volatile uint32_t*q=(void*)((uint8_t*)app->controls+66048);
 if(q[0]!=0x50465349)return 0;__atomic_thread_fence(__ATOMIC_ACQUIRE);uint32_t id=q[1],op=q[2],value=q[3],rc=0;q[0]=0;
 if(!id||id==s->settings_request||(s->phase!=1&&s->phase!=3&&s->phase!=7))rc=0xf320;
 else if(op<PS1_OPTION_COUNT&&value<ps1_options[op].count){s->settings.prefs.value[op]=value;s->settings.dirty=1;rc=ps1_settings_save(&s->settings,&s->sd);if(!rc)apply_live(s);}
 else if(op==100&&value<PS1_CATEGORIES&&(s->phase==1||s->phase==3)){s->settings_return=s->phase;ps1_settings_menu_init(&s->settings_menu,menu_buttons(s));s->settings_menu.category=value;s->phase=7;}
 else if(op==101&&s->phase==7)s->settings_menu.pending_close=1;
 else if(op==108&&s->phase==7&&value<PS1_PLAYER_COUNT)s->settings_menu.player=value;
 else if(op>=210&&op<214&&value<PS1_DEVICE_COUNT){s->settings.prefs.controller_device[op-210]=value;s->settings.dirty=1;rc=ps1_settings_save(&s->settings,&s->sd);}
 else if(op>=200&&op<200+PS1_CONTROL_COUNT&&value<ps1_options[PS1_SWAP_AB+op-200].count){s->settings.prefs.player2[op-200]=value;s->settings.dirty=1;rc=ps1_settings_save(&s->settings,&s->sd);}
 else if(((op>=230&&op<234)||(op>=240&&op<244))&&value<ps1_options[PS1_SWAP_AB+op%10].count){unsigned player=op/10-21;while(ps1_control_value(&s->settings.prefs,player,PS1_SWAP_AB+op%10)!=value)ps1_control_change(&s->settings,player,PS1_SWAP_AB+op%10,1);rc=ps1_settings_save(&s->settings,&s->sd);}
 else if(op==220&&value<PS1_MULTITAP_COUNT){s->settings.prefs.multitap=value;s->settings.dirty=1;rc=ps1_settings_save(&s->settings,&s->sd);}
 else if(op==104&&app->controls_bytes>=0x300000u+640u*480u*4u){s->capture_request=id;s->settings_request=id;return 0;}
 else rc=0xf321;
 s->settings_request=id;q[5]=rc?3:2;q[6]=rc;__atomic_thread_fence(__ATOMIC_RELEASE);q[4]=id;return s->sd.transport?rc:0;
}
static uint32_t bind(IzNativeApp*app,State*s){
 if(!ps1_sd_public_app(app))return 0xf202;
 const IzNativeEnvironment*e=ps1_sd_environment(app);if(!e)return 0xf203;
 uint64_t sdk=e->sdk;
 /* Check every byte before calling even memcpy/memset via the import table. */
 for(unsigned i=0;i<PS1_IMPORT_COUNT;i++){if(izpsx_imports[i].offset>e->sdk_bytes||32>e->sdk_bytes-izpsx_imports[i].offset)return 0xf204;const volatile uint8_t*p=(void*)(uintptr_t)(sdk+izpsx_imports[i].offset);for(unsigned j=0;j<32;j++)if(p[j]!=izpsx_imports[i].bytes[j])return 0xf204;}
 for(unsigned i=0;i<PS1_IMPORT_COUNT;i++)ps1_imports[i]=sdk+izpsx_imports[i].offset;
 memset(s,0,sizeof *s);s->owner=&s->services;s->app=(void*)app;
 ps1_sd_copy_services(s->owner,app,e);
 for(unsigned i=0;i<F_COUNT;i++){
  const volatile uint8_t*p=(void*)(uintptr_t)(sdk+izpsx_services[i].offset);
  if(izpsx_services[i].offset>e->sdk_bytes||izpsx_services[i].length>e->sdk_bytes-izpsx_services[i].offset)return 0xf204;
  for(unsigned j=0;j<izpsx_services[i].length;j++)if(p[j]!=izpsx_services[i].bytes[j])return 0xf204;
  s->owner->functions[i]=sdk+izpsx_services[i].offset;
 }
 for(unsigned i=0;i<PS1_IMPORT_COUNT;i++)s->imports[i]=ps1_imports[i];
 return 0;
}
static uint32_t begin(IzNativeApp*app,State*s){
 uint32_t rc=bind(app,s);if(rc)return rc;
 const IzNativeEnvironment*environment=ps1_sd_environment(app);
 rc=ps1_controllers_bind(&s->controllers,environment->sdk,environment->sdk_bytes);if(rc)return rc;
 /* A returned rejection keeps P1 usable and reports P2 unavailable in Controls. */
 ps1_controllers_enable(&s->controllers);
 rc=ps1_controllers_poll(&s->controllers);if(rc)return rc;
 s->previous=s->controllers.player[0].buttons;
 IzNativeApp2*x=(void*)app;
 if(!x->audio||x->audio->version!=1||x->audio->bytes!=sizeof(IzNativeAudio)||!x->private_memory||x->private_bytes!=IZPSX_PRIVATE_BYTES)return 0xf205;
 for(unsigned i=0;i<IZ_AUDIO_FUNCTIONS;i++)if(!x->audio->functions[i])return 0xf205;
 const uint64_t sizes[]={HEAP_BYTES,STACK_BYTES,JIT_BYTES,JIT_BYTES,131072,1024*512*4};
 for(unsigned i=0;i<6;i++){rc=allocate(s,i,sizes[i]);if(rc)return fault(s,rc);}
 s->stack=s->allocations[1];IzpsxBridge*b=app->data;b->stack_top=s->stack+STACK_BYTES;
 Ps1Context*p=&s->context;HeavyContext*h=&p->base;
 h->magic=HEAVY_MAGIC;h->version=1;h->flags=PS1_FLAG_BIOS_REQUIRED|PS1_FLAG_MEMORY_CARDS|PS1_FLAG_PAGE_PUBLICATION;
 h->heap=s->allocations[0];h->heap_bytes=HEAP_BYTES;h->tick=s->owner->functions[F_TICK];h->tick_hz=app->tick_hz;h->frame=s->allocations[5];h->frame_capacity=1024*512*4;
 h->wad=(uintptr_t)&s->io;h->reserved=(uintptr_t)&s->audio;h->error_text=(uintptr_t)s->log;h->error_capacity=sizeof s->log;
 p->imports=(uintptr_t)s->imports;p->width=640;p->height=480;
 p->jit_source=s->allocations[2];p->jit_rw=s->allocations[3];p->jit_bytes=JIT_BYTES;
 memset((void*)(uintptr_t)h->frame,0,h->frame_capacity);memset((void*)(uintptr_t)p->jit_source,0,JIT_BYTES);memset((void*)(uintptr_t)p->jit_rw,0,JIT_BYTES);
 p->pid=s->owner->pid;p->ro_pid=s->owner->ro_pid;p->service=s->owner->ro_handle;p->send=s->owner->functions[F_SEND];
 IzAddressPlan plan;rc=iz_address_find(s->owner,JIT_BYTES,&plan);if(rc)return fault(s,rc);p->jit_rx=plan.code_base;
 s->sd=(Ps1SdDirectory){.send=(void*)(uintptr_t)s->owner->functions[F_SEND],.close_handle=(void*)(uintptr_t)s->owner->functions[F_CLOSE],.filesystem=s->owner->filesystem,.buffer=(void*)(uintptr_t)s->allocations[4],.capacity=65536};
 s->io=(Ps1FileIO){.send=s->owner->functions[F_SEND],.buffer=s->allocations[4]+65536,.capacity=65536,.directory=(uintptr_t)&s->sd};
 const unsigned order[]={0,1,2,3,4,5,6,7,8,11,9,10,12};for(unsigned i=0;i<13;i++)s->audio.functions[i]=(uintptr_t)x->audio->functions[order[i]];
 s->audio.arena=(uintptr_t)x->private_memory+0x410000;s->audio.arena_bytes=65536;s->audio.sleep=s->owner->functions[F_SLEEP];
 rc=ps1_sd_jit_probe(p);if(rc)return fault(s,rc);s->jit_ready=64;
 ps1_context=p;ps1_platform_init();rc=ps1_settings_load(&s->settings,&s->sd);if(rc)return fault(s,rc);ps1_controllers_assign(&s->controllers,s->settings.prefs.controller_device);s->previous=s->controllers.menu.buttons;s->menu_source=s->controllers.menu.connected?(s->controllers.menu.id|(s->controllers.menu.style<<8)):UINT32_MAX;ps1_active_settings=&s->settings.prefs;apply_live(s);Ps1MenuConfig ui=config(s);rc=ps1_menu_init(&s->menu,&ui);if(rc)return fault(s,rc);
 s->phase=1;return 0;
}
static uint32_t end_game(State*s){
 if(!s->core_live)return 0;if(!s->replay.frames&&ps1_audio_pause_now())return fault(s,0xf209);uint32_t rc=ps1_sd_core_end();if(rc)return fault(s,rc);
 s->core_live=0;if(s->context.pages_mapped||s->context.mapping_unknown||s->audio.opened||s->audio.pending||s->audio.uncertain||s->io.opened||s->sd.leaked_handle)return fault(s,0xf206);return 0;
}
/* Called by the PIC owner on its own stack, after STOP's stack switch returns. */
uint32_t izpsx_release_stack(IzNativeApp*app){IzpsxBridge*b=app->data;State*s=(void*)((uint8_t*)app->data+IZPSX_STATE_OFFSET);if(s->allocations[1]){CALL(s->owner,F_FREE,s->allocations[1],0,0,0,0,0,0,0);s->stack=0;s->allocations[1]=0;}b->stack_top=0;return 0;}
uint32_t izpsx_engine_event(IzNativeApp*app,uint32_t event){
 State*s=(void*)((uint8_t*)app->data+IZPSX_STATE_OFFSET);IzpsxBridge*b=app->data;
 if(event==IZ_NATIVE_INIT){uint32_t rc=begin(app,s);b->state=(uintptr_t)s;return rc;}
 ps1_context=&s->context;
 if(event==IZ_NATIVE_STOP){
  if(s->uncertain||(s->failed&&(s->core_live||s->context.mapping_unknown||s->sd.leaked_handle)))return s->failed?s->failed:0xf201;
  uint32_t rc=end_game(s);if(rc)return rc;
  if(s->settings.dirty){rc=ps1_settings_save(&s->settings,&s->sd);if(rc)return fault(s,rc);}
  rc=ps1_controllers_restore(&s->controllers);if(rc)return fault(s,rc);
  if(s->sd.leaked_handle||s->io.opened||s->context.pages_mapped||s->context.mapping_unknown)return 0xf206;
  for(int i=5;i>=0;i--)if(i!=1&&s->allocations[i]){CALL(s->owner,F_FREE,s->allocations[i],0,0,0,0,0,0,0);s->allocations[i]=0;}return 0;
 }
 if(event!=IZ_NATIVE_FRAME)return 0xf207;
 uint32_t input_error=ps1_controllers_poll(&s->controllers);if(input_error)return fault(s,input_error);
 uint32_t qualification=qualify_settings(s);if(qualification)return fault(s,qualification);
 ps1_controllers_assign(&s->controllers,s->settings.prefs.controller_device);
 Ps1MenuConfig ui=config(s);uint64_t now=CALL(s->owner,F_TICK,0,0,0,0,0,0,0,0),raw=s->controllers.menu.buttons;
 uint32_t menu_source=s->controllers.menu.connected?(s->controllers.menu.id|(s->controllers.menu.style<<8)):UINT32_MAX;
 if(menu_source!=s->menu_source){s->menu_source=menu_source;s->previous=raw;s->settings_menu.armed=0;s->settings_menu.previous=menu_buttons(s);s->pause.input_armed=0;s->pause.previous=menu_buttons(s);s->menu.previous=menu_buttons(s);s->menu.held_ticks=0;}
 if(s->phase!=1){volatile uint32_t*q=(void*)((uint8_t*)app->controls+848);if(q[0]==IZPSX_REPLAY_MAGIC){q[0]=0;q[5]=3;q[6]=0xf221;__atomic_thread_fence(__ATOMIC_RELEASE);q[4]=q[1];}}
 uint32_t edge=(uint32_t)(raw&~s->previous);s->previous=raw;
 if(s->phase==1){
  if(edge&0x800){s->settings_return=1;ps1_settings_menu_init(&s->settings_menu,menu_buttons(s));s->phase=7;return 0;}
  if((edge&8)&&s->settings.prefs.value[PS1_REMEMBER_GAME]&&s->settings.prefs.recent[0]){uint64_t size=0;uint32_t rc=ps1_sd_statfile(&s->sd,IZPSX_SD_GAMES,s->settings.prefs.recent,&size);if(!rc){strcpy(s->menu.selected,s->settings.prefs.recent);s->replay.frames=0;s->phase=5;return 0;}s->menu.error=rc;}
  int request=ps1_replay_take(&s->replay,app->controls,app->controls_bytes,IZPSX_SD_GAMES);
  if(request==1){strcpy(s->menu.selected,s->replay.path);ps1_loading_draw(ui.rgba,1000,"STARTING YOUR GAME");publish(s);s->phase=5;return 0;}
  ps1_menu_step(&s->menu,&ui,menu_buttons(s));ps1_menu_draw(&s->menu,&ui);s->context.width=640;s->context.height=480;publish(s);
  if(s->menu.status==PS1_MENU_CANCELLED||s->menu.status==PS1_MENU_EXIT){app->request_exit(app);return 0;}
  if(s->menu.status!=PS1_MENU_SELECTED)return 0;
  /* Show one complete loading frame before entering the BIOS/core. */
  s->replay.frames=0;ps1_loading_draw(ui.rgba,1000,"STARTING YOUR GAME");publish(s);s->phase=5;return 0;
 }
 if(s->phase==5){
  uint64_t game_size=0;uint32_t rc=ps1_sd_statfile(&s->sd,IZPSX_SD_GAMES,s->menu.selected,&game_size);
  if(rc||!game_size){if(s->sd.transport||s->sd.leaked_handle)return fault(s,rc?rc:0xf20c);return library_error(s,rc?rc:0xf20c,2);}
  uint64_t bios_size=0;rc=ps1_sd_statfile(&s->sd,IZPSX_SD_ROOT,IZPSX_SD_BIOS,&bios_size);
  if(rc||bios_size!=512*1024){if(s->sd.transport||s->sd.leaked_handle)return fault(s,rc?rc:0xf208);return library_error(s,rc?rc:0xf208,1);}
  strcpy(s->game,"/ps1");strcat(s->game,s->menu.selected+strlen(IZPSX_SD_GAMES));s->context.game_path=(uintptr_t)s->game;
  if(s->settings.prefs.value[PS1_REMEMBER_GAME]&&strcmp(s->settings.prefs.recent,s->menu.selected)){strcpy(s->settings.prefs.recent,s->menu.selected);s->settings.dirty=1;rc=ps1_settings_save(&s->settings,&s->sd);if(rc)return fault(s,rc);}
  s->launched=s->settings.prefs;ps1_active_settings=&s->launched;s->context.interpreter=s->launched.value[PS1_CPU];apply_live(s);
  s->core_live=1;rc=ps1_sd_core_begin(&s->context);
  if(rc==63&&!s->context.pages_mapped&&!s->context.mapping_unknown&&!s->audio.opened&&!s->audio.pending&&!s->audio.uncertain&&!s->io.opened&&!s->sd.leaked_handle&&!s->sd.transport){s->core_live=0;s->context.base.error=0;return library_error(s,rc,2);}
  if(rc)return fault(s,rc);
  s->rate_tick=s->context.base.run_start;s->rate_frames=s->rate_pictures=s->pictures=0;
  s->phase=2;return 0;
 }
 if(s->phase==2){
  if(edge&0x800){
   s->pause_start=now;if(ps1_audio_pause_now())return fault(s,0xf209);
   uint32_t rc=ps1_cards_poll(now,app->tick_hz,1);if(rc)return fault(s,rc);
   s->game_w=s->context.width;s->game_h=s->context.height;s->game_calls=s->context.video_calls;
   memcpy((uint8_t*)((IzNativeApp2*)app)->private_memory+0x210000,(void*)(uintptr_t)s->context.base.frame,(size_t)s->game_w*s->game_h*4);
   if(ps1_pause_init(&s->pause,&ui))return fault(s,0xf20a);s->phase=3;
  }else{
   uint64_t frames=ps1_game_frames_done(),fps=s->context.reserved,scale=app->tick_hz*1000000ull;
   uint64_t due=s->context.base.run_start+frames*(scale/fps)+(frames/fps)*(scale%fps)+((frames%fps)*(scale%fps))/fps;
   uint64_t ahead=due-now;if(ahead&&ahead<=(uint64_t)INT64_MAX)return 0;
   uint32_t mask=0,input[PS1_PLAYER_COUNT]={0};
   for(unsigned i=0;i<PS1_PLAYER_COUNT;i++){const Ps1Controller*p=&s->controllers.player[i];mask|=p->connected<<i;input[i]=ps1_input_player_settings(&s->settings.prefs,i,ps1_controller_buttons(p->buttons),p->axes,now,app->tick_hz);}
   if(!s->launched.multitap){mask&=3;input[2]=input[3]=0;}
   if(s->replay.frames){mask=3;memset(input,0,sizeof input);if(frames<s->replay.frames)input[0]=s->replay.buttons[frames];}
   ps1_sd_core_ports(mask);uint32_t rc=ps1_sd_core_frame4(input[0],input[1],input[2],input[3]);if(rc)return fault(s,rc);
   now=CALL(s->owner,F_TICK,0,0,0,0,0,0,0,0);if(now-s->rate_tick>=app->tick_hz){s->emu_rate=(unsigned)((ps1_game_frames_done()-s->rate_frames)*app->tick_hz/(now-s->rate_tick));s->picture_rate=(unsigned)((s->pictures-s->rate_pictures)*app->tick_hz/(now-s->rate_tick));s->rate_tick=now;s->rate_frames=ps1_game_frames_done();s->rate_pictures=s->pictures;}
   show_picture(s,now,0);
   if(s->replay.frames&&ps1_game_frames_done()>=s->replay.frames){
    rc=end_game(s);if(rc)return rc;s->phase=6;
    volatile uint64_t*result=(void*)((uint8_t*)app->controls+888);result[0]=s->context.base.run_start;result[1]=s->context.base.run_end;result[2]=s->io.reads;result[3]=s->io.bytes;result[4]=s->audio.output_frames;
    volatile uint32_t*done=(void*)((uint8_t*)app->controls+928);done[0]=(uint32_t)s->audio.opened;done[1]=s->context.pages_mapped;done[2]=(uint32_t)s->io.opened;done[3]=(uint32_t)s->audio.pending;
    /* Snapshot only an explicitly requested finite test. The parent controls
     * outlive app data; host observers need no live module/GPU pointers. */
    uint32_t image_bytes=s->context.width*s->context.height*4;
    if(app->controls_bytes>=0x100000u+image_bytes&&image_bytes<=1024u*512u*4u){
     memcpy((uint8_t*)app->controls+0x100000,(void*)(uintptr_t)s->context.base.frame,image_bytes);
     volatile uint32_t*shot=(void*)((uint8_t*)app->controls+944);shot[0]=0x46585049;shot[1]=s->context.width;shot[2]=s->context.height;shot[3]=image_bytes;
     *(volatile uint64_t*)((uint8_t*)app->controls+960)=s->context.base.image_hash;
    }
    if(app->controls_bytes>=32768+sizeof s->log)memcpy((uint8_t*)app->controls+32768,s->log,sizeof s->log);
    *(volatile uint32_t*)((uint8_t*)app->controls+976)=s->context.base.flags;
    *(volatile uint32_t*)((uint8_t*)app->controls+980)=s->context.base.changed_frames;
    __atomic_thread_fence(__ATOMIC_RELEASE);*(volatile uint32_t*)((uint8_t*)app->controls+868)=2;
   }
   publish(s);return 0;
  }
 }
 if(s->phase==3){
  ui.save_status="MEMORY CARDS SAVED TO SD";ps1_pause_step(&s->pause,&ui,menu_buttons(s));ps1_pause_draw(&s->pause,&ui);s->context.width=640;s->context.height=480;publish(s);
  if(s->pause.status==PS1_PAUSE_CONTINUE){
   memcpy((void*)(uintptr_t)s->context.base.frame,(uint8_t*)((IzNativeApp2*)app)->private_memory+0x210000,(size_t)s->game_w*s->game_h*4);s->context.width=s->game_w;s->context.height=s->game_h;s->context.video_calls=s->game_calls;
   apply_live(s);if(ps1_audio_resume())return fault(s,0xf20b);s->context.base.run_start+=now-s->pause_start;s->rate_tick=now;s->rate_frames=ps1_game_frames_done();s->rate_pictures=s->pictures;s->phase=2;show_picture(s,now,1);publish(s);
  }else if(s->pause.status==PS1_PAUSE_SETTINGS){s->settings_return=3;ps1_settings_menu_init(&s->settings_menu,menu_buttons(s));s->phase=7;
  }else if(s->pause.status==PS1_PAUSE_RESTART){
   s->context.width=s->game_w;s->context.height=s->game_h;uint32_t rc=ps1_sd_core_reset();if(rc)return fault(s,rc);apply_live(s);if(ps1_audio_resume())return fault(s,0xf20b);s->rate_tick=now;s->rate_frames=s->rate_pictures=s->pictures=0;s->phase=2;
  }else if(s->pause.status==PS1_PAUSE_CHOOSE_GAME||s->pause.status==PS1_PAUSE_RETURN_LAUNCHER){
   uint32_t rc=end_game(s);if(rc)return rc;
   if(s->pause.status==PS1_PAUSE_CHOOSE_GAME)b->reload=1;else app->request_exit(app);
  }
 }
 if(s->phase==7){
  ps1_settings_menu_step(&s->settings_menu,&s->settings,menu_buttons(s));
  if(s->settings_menu.disconnect_request){unsigned player=s->settings_menu.disconnect_request-1;s->settings_menu.disconnect_request=0;uint32_t rc=ps1_controllers_disconnect(&s->controllers,player);s->settings_menu.action_error=rc;s->settings_menu.disconnected=!rc;}
  ps1_settings_menu_draw(&s->settings_menu,&s->settings,&ui);s->context.width=640;s->context.height=480;publish(s);
  if(s->settings_menu.closed){uint32_t rc=ps1_settings_save(&s->settings,&s->sd);if(rc){if(s->sd.transport)return fault(s,rc);s->settings_menu.closed=s->settings_menu.pending_close=0;s->settings_menu.error=rc;return 0;}apply_live(s);s->phase=s->settings_return;if(s->phase==3)ps1_pause_init(&s->pause,&ui);}
 }
 return 0;
}
