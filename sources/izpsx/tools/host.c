#define _GNU_SOURCE
#include <assert.h>
#include <dlfcn.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "libretro.h"
static struct {char*key,*value;} variables[256];
static unsigned variable_count,pixel_format,width,height,video_calls,changed_frames;
static uint8_t *rgba;
static uint64_t image_hash,audio_hash=1469598103934665603ull,audio_frames,previous_hash;
static FILE *trace,*audio_file;
static uint32_t *input_script,input_frame;
static uint64_t hash(const void*data,size_t n,uint64_t h){const uint8_t*p=data;while(n--){h^=*p++;h*=1099511628211ull;}return h;}
static double seconds(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
static void log_message(enum retro_log_level level,const char*format,...){va_list args;va_start(args,format);vfprintf(stderr,format,args);va_end(args);}
static bool environment(unsigned command,void*data){
 switch(command){
 case RETRO_ENVIRONMENT_SET_VARIABLES:{
  const struct retro_variable*v=data;
  for(;v->key;v++){
   const char*start=strchr(v->value,';');if(!start)continue;start++;while(*start==' ')start++;
   size_t length=strcspn(start,"|");assert(variable_count<256);variables[variable_count++]=(typeof(variables[0])){strdup(v->key),strndup(start,length)};
  }return true;
 }
 case RETRO_ENVIRONMENT_GET_VARIABLE:{
  struct retro_variable*v=data;v->value=NULL;
  if(!strcmp(v->key,"pcsx_rearmed_bios"))v->value=getenv("PS1_BIOS_DIR")?"auto":"HLE";
  else if(!strcmp(v->key,"pcsx_rearmed_rgb32_output"))v->value="enabled";
  else if(!strcmp(v->key,"pcsx_rearmed_drc"))v->value=getenv("PS1_INTERPRETER")?"disabled":"enabled";
  else if(!strcmp(v->key,"pcsx_rearmed_psxclock"))v->value="57";
  else for(unsigned i=0;i<variable_count;i++)if(!strcmp(v->key,variables[i].key)){v->value=variables[i].value;break;}
  return v->value!=NULL;
 }
 case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:*(bool*)data=false;return true;
 case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:*(unsigned*)data=0;return true;
 case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:((struct retro_log_callback*)data)->log=log_message;return true;
 case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:pixel_format=*(unsigned*)data;return true;
 case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
 case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:*(const char**)data=getenv("PS1_BIOS_DIR")?getenv("PS1_BIOS_DIR"):"/tmp/ps1-native-empty";return true;
 case RETRO_ENVIRONMENT_GET_CAN_DUPE:*(bool*)data=true;return true;
 case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:return true;
 case RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS:
 case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
 case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
 case RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE:
 case RETRO_ENVIRONMENT_SET_SUBSYSTEM_INFO:
 case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
 case RETRO_ENVIRONMENT_SET_GEOMETRY:
 case RETRO_ENVIRONMENT_SET_MESSAGE:return true;
 default:return false;
 }
}
static void video(const void*data,unsigned w,unsigned h,size_t pitch){
 video_calls++;if(!data)return;
 assert(w<=1024&&h<=512);width=w;height=h;
 for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++){
  const uint8_t*p=(const uint8_t*)data+y*pitch;uint32_t c;
  if(pixel_format==RETRO_PIXEL_FORMAT_XRGB8888)c=((const uint32_t*)p)[x];
  else {uint16_t a=((const uint16_t*)p)[x];unsigned r=pixel_format==RETRO_PIXEL_FORMAT_RGB565?(a>>11)&31:(a>>10)&31,g=pixel_format==RETRO_PIXEL_FORMAT_RGB565?(a>>5)&63:(a>>5)&31,b=a&31;c=(r*255/31)<<16|(g*255/(pixel_format==RETRO_PIXEL_FORMAT_RGB565?63:31))<<8|b*255/31;}
  size_t at=((size_t)y*w+x)*4;rgba[at]=c>>16;rgba[at+1]=c>>8;rgba[at+2]=c;rgba[at+3]=255;
 }
 image_hash=hash(rgba,(size_t)w*h*4,1469598103934665603ull);
 if(previous_hash&&previous_hash!=image_hash)changed_frames++;previous_hash=image_hash;
}
static size_t audio(const int16_t*data,size_t frames){audio_hash=hash(data,frames*4,audio_hash);audio_frames+=frames;if(audio_file)fwrite(data,4,frames,audio_file);return frames;}
static void audio_sample(int16_t a,int16_t b){int16_t values[2]={a,b};audio(values,1);}
static void input_poll(void){}
static int16_t input_state(unsigned port,unsigned device,unsigned index,unsigned id){if(port||device!=RETRO_DEVICE_JOYPAD||!input_script)return 0;unsigned bits=input_script[input_frame];return id==RETRO_DEVICE_ID_JOYPAD_MASK?(int16_t)bits:id<16?(bits>>id)&1:0;}
int main(int argc,char**argv){
 if(argc!=5){fprintf(stderr,"usage: host CORE.so GAME FRAMES OUTPUT\n");return 2;}
 #ifndef PS1_STATIC_CORE
 void*core=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);if(!core){fprintf(stderr,"%s\n",dlerror());return 3;}
 #define FUNCTION(name,type) __typeof__(type) name=(type)dlsym(core,#name);assert(name)
 #else
 #define FUNCTION(name,type) (void)&name
 #endif
 FUNCTION(retro_set_environment,void(*)(retro_environment_t));FUNCTION(retro_set_video_refresh,void(*)(retro_video_refresh_t));FUNCTION(retro_set_audio_sample_batch,void(*)(retro_audio_sample_batch_t));FUNCTION(retro_set_audio_sample,void(*)(retro_audio_sample_t));FUNCTION(retro_set_input_poll,void(*)(retro_input_poll_t));FUNCTION(retro_set_input_state,void(*)(retro_input_state_t));
 FUNCTION(retro_init,void(*)(void));FUNCTION(retro_deinit,void(*)(void));FUNCTION(retro_load_game,bool(*)(const struct retro_game_info*));FUNCTION(retro_unload_game,void(*)(void));FUNCTION(retro_run,void(*)(void));FUNCTION(retro_get_system_av_info,void(*)(struct retro_system_av_info*));
 rgba=calloc(1024*512,4);retro_set_environment(environment);retro_set_video_refresh(video);retro_set_audio_sample_batch(audio);retro_set_audio_sample(audio_sample);retro_set_input_poll(input_poll);retro_set_input_state(input_state);
 char filename[4096];snprintf(filename,sizeof filename,"%s.trace",argv[4]);trace=fopen(filename,"w");snprintf(filename,sizeof filename,"%s.pcm",argv[4]);audio_file=fopen(filename,"wb");assert(trace&&audio_file);
 double boot=seconds();retro_init();struct retro_game_info game={.path=argv[2]};if(!retro_load_game(&game)){fprintf(stderr,"game load failed\n");return 4;}
 struct retro_system_av_info av={0};retro_get_system_av_info(&av);boot=seconds()-boot;
 unsigned frames=strtoul(argv[3],0,10);const char*input_path=getenv("PS1_INPUT_FILE");if(input_path){FILE*in=fopen(input_path,"rb");assert(in);input_script=calloc(frames,4);assert(fread(input_script,4,frames,in)==frames);fclose(in);}double start=seconds();for(input_frame=0;input_frame<frames;input_frame++){retro_run();if(trace)fprintf(trace,"%u %u %u %016llx\n",input_frame+1,width,height,(unsigned long long)image_hash);}double elapsed=seconds()-start;
 snprintf(filename,sizeof filename,"%s.rgba",argv[4]);FILE*f=fopen(filename,"wb");assert(f);fwrite(rgba,4,(size_t)width*height,f);fclose(f);fclose(trace);fclose(audio_file);
 printf("{\"framesRequested\":%u,\"videoCallbacks\":%u,\"changedFrames\":%u,\"width\":%u,\"height\":%u,\"imageHash\":\"%016llx\",\"audioFrames\":%llu,\"audioHash\":\"%016llx\",\"emulatedFps\":%.8f,\"sampleRate\":%.2f,\"bootSeconds\":%.6f,\"runSeconds\":%.6f,\"unpacedCoreFramesPerSecond\":%.2f}\n",frames,video_calls,changed_frames,width,height,(unsigned long long)image_hash,(unsigned long long)audio_frames,(unsigned long long)audio_hash,av.timing.fps,av.timing.sample_rate,boot,elapsed,frames/elapsed);
 retro_unload_game();retro_deinit();
 #ifndef PS1_STATIC_CORE
 dlclose(core);
 #endif
 return width?0:5;
}
