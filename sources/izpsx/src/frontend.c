#include "platform.h"
#include "display.h"
#include "audio.h"
#include "resident.h"
#include "memory-card.h"
#include "settings.h"
#include "sio.h"
#include "libretro.h"
#include "psxcommon.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
static void *panic_frame[5];
static struct {char*key,*value;} variables[256];
static unsigned variable_count;
static uint64_t previous_hash;
static uint32_t live_buttons[PS1_PLAYER_COUNT];
static uint64_t completed_frames;
uint64_t ps1_game_frames_done(void){return completed_frames;}
static int game_config_valid(Ps1Context*c){
 HeavyContext*b=&c->base;int continuous=(b->flags&PS1_FLAG_CONTINUOUS)!=0;
 /* Also bounds the remainder * 1e9 conversion used by native sleep. */
 if(!b->tick||!b->tick_hz||b->tick_hz>UINT64_MAX/1000000000ull||!b->heap||b->heap_bytes<16*1024*1024||b->frame_capacity<1024*512*4)return 0;
 if((!b->trace)!=(!b->trace_capacity))return 0;
 if(continuous)return c->autonomy&&b->display&&!c->input&&(!b->trace_capacity||b->trace_capacity>=sizeof(HeavyTrace));
 return b->frames_target&&b->frames_target<=100000&&b->trace_capacity/sizeof(HeavyTrace)>=b->frames_target;
}
static uint64_t fingerprint(const void*data,size_t n,uint64_t h){const uint8_t*p=data;while(n--){h^=*p++;h*=1099511628211ull;}return h;}
static uint64_t tick(void){return ((uint64_t(*)(void))(uintptr_t)ps1_context->base.tick)();}
void ps1_fatal(unsigned code){ps1_context->base.error=code;ps1_context->base.phase=0xff;__builtin_longjmp(panic_frame,1);}
void ps1_log(const char*s){HeavyContext*b=&ps1_context->base;size_t at=ps1_context->log_bytes;if(!b->error_text)return;char*out=(void*)(uintptr_t)b->error_text;while(*s&&at+1<b->error_capacity)out[at++]=*s++;if(at<b->error_capacity)out[at]=0;ps1_context->log_bytes=at;}
static void log_message(enum retro_log_level level,const char*format,...){char b[1024];va_list a;va_start(a,format);vsnprintf(b,sizeof b,format,a);va_end(a);ps1_log(b);}
static bool environment(unsigned command,void*data){
 switch(command){
 case RETRO_ENVIRONMENT_SET_VARIABLES:{const struct retro_variable*v=data;for(;v->key;v++){const char*s=strchr(v->value,';');if(!s)continue;s++;while(*s==' ')s++;size_t n=0;while(s[n]&&s[n]!='|')n++;if(variable_count>=256)ps1_fatal(61);variables[variable_count++]=(typeof(variables[0])){strdup(v->key),strndup(s,n)};}return true;}
 case RETRO_ENVIRONMENT_GET_VARIABLE:{struct retro_variable*v=data;v->value=NULL;const char*setting=ps1_core_setting(v->key);if(setting)v->value=setting;else if((ps1_context->base.flags&PS1_FLAG_MEMORY_CARDS)&&(!strcmp(v->key,"pcsx_rearmed_memcard1")||!strcmp(v->key,"pcsx_rearmed_memcard2")))v->value="libretro";else if(!strcmp(v->key,"pcsx_rearmed_bios"))v->value=(ps1_context->base.flags&PS1_FLAG_BIOS_REQUIRED)?"auto":"HLE";else if(!strcmp(v->key,"pcsx_rearmed_rgb32_output"))v->value="enabled";else if(!strcmp(v->key,"pcsx_rearmed_drc"))v->value=ps1_context->interpreter?"disabled":"enabled";else if(!strcmp(v->key,"pcsx_rearmed_psxclock"))v->value="57";else for(unsigned i=0;i<variable_count;i++)if(!strcmp(v->key,variables[i].key)){v->value=variables[i].value;break;}return v->value!=NULL;}
 case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:*(bool*)data=false;return true;
 case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:*(unsigned*)data=0;return true;
 case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:((struct retro_log_callback*)data)->log=log_message;return true;
 case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:ps1_context->pixel_format=*(unsigned*)data;return true;
 case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:*(const char**)data="/ps1";return true;
 case RETRO_ENVIRONMENT_GET_CAN_DUPE:*(bool*)data=true;return true;
 case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:return true;
 case RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS:case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:case RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE:case RETRO_ENVIRONMENT_SET_SUBSYSTEM_INFO:case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:case RETRO_ENVIRONMENT_SET_GEOMETRY:case RETRO_ENVIRONMENT_SET_MESSAGE:return true;
 default:return false;
 }
}
static void video(const void*data,unsigned w,unsigned h,size_t pitch){
 Ps1Context*c=ps1_context;HeavyContext*b=&c->base;c->video_calls++;if(!data)return;
 if(w>1024||h>512||(uint64_t)w*h*4>b->frame_capacity)ps1_fatal(62);c->width=w;c->height=h;
 uint8_t*rgba=(void*)(uintptr_t)b->frame;
 for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++){
  const uint8_t*p=(const uint8_t*)data+y*pitch;uint32_t color;
  if(c->pixel_format==RETRO_PIXEL_FORMAT_XRGB8888)color=((const uint32_t*)p)[x];else {uint16_t a=((const uint16_t*)p)[x];unsigned r=c->pixel_format==RETRO_PIXEL_FORMAT_RGB565?(a>>11)&31:(a>>10)&31,g=c->pixel_format==RETRO_PIXEL_FORMAT_RGB565?(a>>5)&63:(a>>5)&31,blue=a&31;color=(r*255/31)<<16|(g*255/(c->pixel_format==RETRO_PIXEL_FORMAT_RGB565?63:31))<<8|blue*255/31;}
  size_t at=((size_t)y*w+x)*4;rgba[at]=color>>16;rgba[at+1]=color>>8;rgba[at+2]=color;rgba[at+3]=255;
 }
 b->image_hash=fingerprint(rgba,(size_t)w*h*4,1469598103934665603ull);if(previous_hash&&previous_hash!=b->image_hash)b->changed_frames++;previous_hash=b->image_hash;
}
static size_t audio(const int16_t*data,size_t frames){Ps1Context*c=ps1_context;c->audio_hash=fingerprint(data,frames*4,c->audio_hash);c->audio_frames+=frames;if(c->audio&&c->audio_bytes<=c->audio_capacity&&frames<= (c->audio_capacity-c->audio_bytes)/4){memcpy((void*)(uintptr_t)(c->audio+c->audio_bytes),data,frames*4);c->audio_bytes+=frames*4;}if(ps1_audio_feed(data,frames))ps1_fatal(141);return frames;}
static void sample(int16_t a,int16_t b){int16_t s[2]={a,b};audio(s,1);}static void poll(void){if(ps1_context->base.display&&!ps1_context->input){memset(live_buttons,0,sizeof live_buttons);live_buttons[0]=ps1_display_input();}}
static int16_t input(unsigned port,unsigned device,unsigned index,unsigned id){
 (void)index;if(port>=PS1_PLAYER_COUNT||device!=RETRO_DEVICE_JOYPAD)return 0;
 /* Existing one-player replay files remain one-player and deterministic. */
 unsigned bits=ps1_context->input?(port?0:((uint32_t*)(uintptr_t)ps1_context->input)[ps1_context->base.frames_done]):live_buttons[port];
 if(id==RETRO_DEVICE_ID_JOYPAD_MASK)return (int16_t)bits;
 return id<16?(int16_t)((bits>>id)&1):0;
}
uint32_t ps1_game_main(HeavyContext*b){
 if(!b||b->magic!=HEAVY_MAGIC||b->version!=1||!game_config_valid((Ps1Context*)b))return 1;
 Ps1Context*c=(void*)b;ps1_context=c;if(!c->imports||c->file_count>64||!c->game_path)return 2;
 Ps1FileIO*io=(void*)(uintptr_t)b->wad;
 if(!c->file_count&&!(c->autonomy&&io&&io->directory))return 2;
 if(c->file_count&&!c->files)return 2;
 /* Every import is initialized before the first library call. */
 for(unsigned i=0;i<PS1_IMPORT_COUNT;i++){ps1_imports[i]=((uintptr_t*)(uintptr_t)c->imports)[i];if(!ps1_imports[i])return 3;}
 b->status=1;b->phase=1;b->error=0;c->audio_hash=1469598103934665603ull;completed_frames=0;
 if(__builtin_setjmp(panic_frame)){b->status=3;return b->error;}
 if(c->service)ps1_ro_map();ps1_platform_init();variable_count=0;previous_hash=0;if(ps1_audio_open())ps1_fatal(140);
 retro_set_environment(environment);retro_set_video_refresh(video);retro_set_audio_sample_batch(audio);retro_set_audio_sample(sample);retro_set_input_poll(poll);retro_set_input_state(input);
 memset(live_buttons,0,sizeof live_buttons);b->boot_start=tick();retro_init();struct retro_game_info game={.path=(const char*)(uintptr_t)c->game_path};if(!retro_load_game(&game))ps1_fatal(63);
 if(b->flags&PS1_FLAG_MEMORY_CARDS){
  if(!io||!io->directory)ps1_fatal(150);
  uint32_t rc=ps1_cards_open((void*)(uintptr_t)io->directory,Mcd1Data,Mcd2Data);
  if(rc){ps1_log("IZPSX: memory card load failed; existing saves preserved.\n");ps1_fatal(151);}
  ps1_log("IZPSX: both SD memory cards loaded and verified.\n");
 }
 if((b->flags&PS1_FLAG_BIOS_REQUIRED)&&Config.HLE)ps1_fatal(66);
 if(!Config.HLE){b->flags|=PS1_FLAG_BIOS_LOADED;ps1_log("Native frontend: real BIOS active; HLE disabled.\n");}
 struct retro_system_av_info av={0};retro_get_system_av_info(&av);if(av.timing.fps<49||av.timing.fps>61)ps1_fatal(64);c->reserved=(uint64_t)(av.timing.fps*1000000);b->boot_end=tick();b->run_start=tick();b->phase=2;b->min_step_ticks=UINT64_MAX;
 const int continuous=(b->flags&PS1_FLAG_CONTINUOUS)!=0;
 while(!b->stop&&(continuous||completed_frames<b->frames_target)){
  if(c->autonomy&&!c->input&&ps1_display_take_menu_request()){
   if(ps1_resident_pause(c))ps1_fatal(144);
   memset(live_buttons,0,sizeof live_buttons);if(b->stop)break;
  }
  if(b->display)ps1_display_pace(completed_frames);
  Ps1FileIO*fs=(void*)(uintptr_t)b->wad;Ps1Audio*out=(void*)(uintptr_t)b->reserved;
  uint64_t patches=c->jit_patches,reads=fs?fs->reads:0,bytes=fs?fs->bytes:0,appends=out?out->appends:0,empty=out?out->empty_ticks:0;
  uint64_t begin=tick();retro_run();uint64_t core_end=tick(),elapsed=core_end-begin;
  if((b->flags&PS1_FLAG_MEMORY_CARDS)&&ps1_cards_poll(core_end,b->tick_hz,0))ps1_fatal(152);
  if(ps1_audio_commit_frame())ps1_fatal(143);
  b->step_ticks+=elapsed;if(elapsed<b->min_step_ticks)b->min_step_ticks=elapsed;if(elapsed>b->max_step_ticks)b->max_step_ticks=elapsed;
  if(completed_frames<b->trace_capacity/sizeof(HeavyTrace))
   ((HeavyTrace*)(uintptr_t)b->trace)[completed_frames]=(HeavyTrace){completed_frames>=UINT32_MAX?UINT32_MAX:(uint32_t)(completed_frames+1),c->input?((uint32_t*)(uintptr_t)c->input)[completed_frames]:live_buttons[0],elapsed,b->image_hash,((uint64_t)c->width<<32)|c->height};
  if(completed_frames!=UINT64_MAX)completed_frames++;
  b->frames_done=completed_frames>UINT32_MAX?UINT32_MAX:(uint32_t)completed_frames;b->last_frame=c->video_calls;if(b->display&&ps1_display_present())ps1_fatal(65);
  if(c->performance&&c->performance_count<c->performance_capacity/sizeof(Ps1Performance)){
   ((Ps1Performance*)(uintptr_t)c->performance)[c->performance_count++]=(Ps1Performance){begin,core_end,tick(),c->jit_patches-patches,(fs?fs->reads:0)-reads,(fs?fs->bytes:0)-bytes,(out?out->appends:0)-appends,(out?out->empty_ticks:0)-empty};
  }
 }

 b->run_end=tick();b->phase=3;if((b->flags&PS1_FLAG_MEMORY_CARDS)&&ps1_cards_poll(tick(),b->tick_hz,1))ps1_fatal(152);retro_unload_game();retro_deinit();for(unsigned i=0;i<variable_count;i++){free(variables[i].key);free(variables[i].value);}if(ps1_audio_close())ps1_fatal(142);if(c->service)ps1_ro_unmap();b->status=2;return 0;
}

uint32_t heavy_main(HeavyContext*b){
 if(!b||b->magic!=HEAVY_MAGIC||b->version!=1)return 1;
 Ps1Context*c=(void*)b;
 if(c->autonomy){
  if(!game_config_valid(c))return 1;
  return ps1_resident_main(c);
 }
 return ps1_game_main(b);
}
