/* Continuous integer 44.1 -> 48 kHz resampling and bounded AudioOut ownership. */
#include "audio.h"
#include <string.h>
enum {INIT,OPEN,RATE,CHANNELS,FORMAT,SET,APPEND,START,STOP,COUNT,FLUSH,RELEASE,CLOSE};
#define SLOTS 8
#define SLOT_BYTES 4096
static unsigned output_volume=100,output_mute,output_mono,startup_buffers=6;
void ps1_audio_config(unsigned volume,unsigned mute,unsigned mono,unsigned buffers){output_volume=volume<=100?volume:100;output_mute=!!mute;output_mono=!!mono;startup_buffers=buffers>=2&&buffers<=6?buffers:6;}
static struct {Ps1Audio*a;uint8_t busy[SLOTS];unsigned slot,frames,have_previous;int16_t previous[2];uint64_t source_frames,next_position,empty_since;void(*pause_poll)(void*);void*pause_argument;} owner;
static uint64_t now(void){return ((uint64_t(*)(void))(uintptr_t)ps1_context->base.tick)();}
static void poll_pause(void){if(owner.pause_poll)owner.pause_poll(owner.pause_argument);}
static void nap(void){poll_pause();((void(*)(uint64_t))(uintptr_t)owner.a->sleep)(1000000);poll_pause();}
static int release_buffers(void){Ps1Audio*a=owner.a;for(unsigned i=0;i<SLOTS;i++){void*p=((void*(*)(void*))(uintptr_t)a->functions[RELEASE])((void*)(uintptr_t)a->arena);if(!p)return 0;uintptr_t at=(uintptr_t)p-(a->arena+512);if(at>=SLOTS*64||at%64||!owner.busy[at/64]||!a->pending){a->uncertain=1;return 1;}owner.busy[at/64]=0;a->pending--;a->releases++;}return 0;}
static int find_slot(void){for(unsigned wait=0;wait<2000;wait++){if(release_buffers())return 1;for(unsigned i=0;i<SLOTS;i++)if(!owner.busy[i]){owner.slot=i;return 0;}nap();}owner.a->uncertain=1;return 2;}
static int start(void){Ps1Audio*a=owner.a;if(((uint32_t(*)(void*))(uintptr_t)a->functions[START])((void*)(uintptr_t)a->arena)){a->uncertain=1;return 1;}a->started=1;return 0;}
static int submit(void){
 Ps1Audio*a=owner.a;if(!owner.frames)return 0;if(owner.busy[owner.slot])return 1;
 void*desc=(void*)(uintptr_t)(a->arena+512+owner.slot*64),*pcm=(void*)(uintptr_t)(a->arena+4096+owner.slot*SLOT_BYTES);
 ((void(*)(void*,void*,uint64_t,uint64_t))(uintptr_t)a->functions[SET])(desc,pcm,SLOT_BYTES,owner.frames*4);
 uint32_t accepted=((uint32_t(*)(void*,void*))(uintptr_t)a->functions[APPEND])((void*)(uintptr_t)a->arena,desc);if(accepted!=1){if(accepted!=0)a->uncertain=1;return 2;}
 if(owner.empty_since){a->empty_ticks+=now()-owner.empty_since;owner.empty_since=0;}
 owner.busy[owner.slot]=1;a->pending++;a->appends++;if(a->pending>a->peak_pending)a->peak_pending=a->pending;owner.frames=0;if(!a->started&&a->pending>=startup_buffers&&start())return 3;return find_slot();
}
int ps1_audio_open(void){
 memset(&owner,0,sizeof owner);owner.a=(void*)(uintptr_t)ps1_context->base.reserved;Ps1Audio*a=owner.a;if(!a)return 0;
 for(unsigned i=0;i<13;i++)if(!a->functions[i])return 1;if(!a->arena||a->arena%4096||a->arena_bytes<4096+SLOTS*SLOT_BYTES||!a->sleep)return 2;
 memset((void*)(uintptr_t)a->arena,0,4096+SLOTS*SLOT_BYTES);void*param=(void*)(uintptr_t)(a->arena+256);((void(*)(void*))(uintptr_t)a->functions[INIT])(param);*(uint32_t*)param=48000;*(uint16_t*)((char*)param+4)=2;
 if(((uint32_t(*)(void*,void*))(uintptr_t)a->functions[OPEN])((void*)(uintptr_t)a->arena,param))return 3;a->opened=1;
 if(((uint32_t(*)(void*))(uintptr_t)a->functions[RATE])((void*)(uintptr_t)a->arena)!=48000||((uint32_t(*)(void*))(uintptr_t)a->functions[CHANNELS])((void*)(uintptr_t)a->arena)!=2||((uint32_t(*)(void*))(uintptr_t)a->functions[FORMAT])((void*)(uintptr_t)a->arena)!=2)return 4;return 0;
}
int ps1_audio_feed(const int16_t*pcm,size_t frames){
 Ps1Audio*a=owner.a;if(!a)return 0;if(!a->opened||a->uncertain)return 1;
 if(release_buffers())return 2;if(a->started&&!a->pending){a->underruns++;if(!owner.empty_since){owner.empty_since=now();a->empty_episodes++;}}
 for(size_t i=0;i<frames;i++){
  uint64_t index=owner.source_frames++;
  if(!owner.have_previous){owner.previous[0]=pcm[i*2];owner.previous[1]=pcm[i*2+1];owner.have_previous=1;continue;}
  while(owner.next_position<=index*160){uint64_t weight=owner.next_position-(index-1)*160;if(weight>160)return 3;
   int16_t*out=(void*)(uintptr_t)(a->arena+4096+owner.slot*SLOT_BYTES+owner.frames*4);
   int32_t fraction=(int32_t)weight;for(unsigned ch=0;ch<2;ch++)out[ch]=((int32_t)owner.previous[ch]*(160-fraction)+(int32_t)pcm[i*2+ch]*fraction)/160;
   if(output_mono){int32_t mixed=((int32_t)out[0]+out[1])/2;out[0]=out[1]=(int16_t)mixed;}
   for(unsigned ch=0;ch<2;ch++)out[ch]=output_mute?0:(int16_t)((int32_t)out[ch]*(int32_t)output_volume/100);
   owner.next_position+=147;owner.frames++;a->output_frames++;if(owner.frames==SLOT_BYTES/4&&submit())return 4;
  }owner.previous[0]=pcm[i*2];owner.previous[1]=pcm[i*2+1];
 }return 0;
}
int ps1_audio_commit_frame(void){
 Ps1Audio*a=owner.a;if(!a)return 0;if(!a->opened||a->uncertain)return 1;
 if(release_buffers())return 2;
 if(a->started&&a->pending<=2&&owner.frames&&submit())return 3;
 return 0;
}
static int pause_drain(void){
 Ps1Audio*a=owner.a;if(!a)return 0;if(!a->opened||a->uncertain||submit())return 1;
 if(a->pending&&!a->started&&start())return 2;
 for(unsigned i=0;i<2000&&a->pending;i++){poll_pause();if(release_buffers())return 3;if(a->pending)nap();}
 poll_pause();
 if(a->pending){a->uncertain=1;return 4;}
 if(a->started){if(((uint32_t(*)(void*))(uintptr_t)a->functions[STOP])((void*)(uintptr_t)a->arena)){a->uncertain=1;return 5;}a->started=0;}
 uint32_t flushed=((uint32_t(*)(void*))(uintptr_t)a->functions[FLUSH])((void*)(uintptr_t)a->arena);
 if(flushed>1||((uint32_t(*)(void*))(uintptr_t)a->functions[COUNT])((void*)(uintptr_t)a->arena)){a->uncertain=1;return 6;}
 owner.empty_since=0;return 0;
}
int ps1_audio_pause_poll(void (*poll)(void *),void *argument){owner.pause_poll=poll;owner.pause_argument=argument;int result=pause_drain();owner.pause_poll=0;owner.pause_argument=0;return result;}
int ps1_audio_pause(void){return ps1_audio_pause_poll(0,0);}
int ps1_audio_pause_now(void){
 Ps1Audio*a=owner.a;if(!a)return 0;if(!a->opened||a->uncertain)return 1;
 if(a->started){if(((uint32_t(*)(void*))(uintptr_t)a->functions[STOP])((void*)(uintptr_t)a->arena)){a->uncertain=1;return 2;}a->started=0;}
 uint32_t flushed=((uint32_t(*)(void*))(uintptr_t)a->functions[FLUSH])((void*)(uintptr_t)a->arena);
 if(flushed>1){a->uncertain=1;return 3;}
 if(release_buffers()||a->pending||((uint32_t(*)(void*))(uintptr_t)a->functions[COUNT])((void*)(uintptr_t)a->arena)){a->uncertain=1;return 4;}
 owner.frames=0;owner.slot=0;owner.empty_since=0;return 0;
}
int ps1_audio_resume(void){Ps1Audio*a=owner.a;if(!a)return 0;return !a->opened||a->uncertain||a->started||a->pending?1:0;}
int ps1_audio_close(void){
 Ps1Audio*a=owner.a;if(!a||!a->opened)return 0;if(a->uncertain||submit())return 1;if(a->pending&&!a->started&&start())return 2;
 for(unsigned i=0;i<2000&&a->pending;i++){if(release_buffers())return 3;if(a->pending)nap();}if(a->pending){a->uncertain=1;return 4;}
 if(a->started){if(((uint32_t(*)(void*))(uintptr_t)a->functions[STOP])((void*)(uintptr_t)a->arena)){a->uncertain=1;return 5;}a->started=0;}
 uint32_t flushed=((uint32_t(*)(void*))(uintptr_t)a->functions[FLUSH])((void*)(uintptr_t)a->arena);if(flushed>1||((uint32_t(*)(void*))(uintptr_t)a->functions[COUNT])((void*)(uintptr_t)a->arena)){a->uncertain=1;return 6;}
 ((void(*)(void*))(uintptr_t)a->functions[CLOSE])((void*)(uintptr_t)a->arena);a->opened=0;return 0;
}
