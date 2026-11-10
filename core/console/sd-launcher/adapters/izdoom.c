/* Port of izdoom-prime18's finite startup/close audio controller. GPU writes
 * the PCM directly; FRAME only performs its two startup handshakes. The
 * supervisor calls us after queue idle, before submitting the next GPU frame.
 * No host request, allocation, audio refill or symbol lookup occurs here. */
#include "native-app.h"
#define FIRST 196u
#define FRAMES 1048576u
#define PCM_OFFSET 67108864u
#define PCM_BYTES 4194304u
#define PRIVATE_BYTES 71303168u
#define BANK_WORDS (32212048u/4u)
#define FAIL 0xd018u
typedef struct {
 _Alignas(16) uint8_t metadata[3072];
 uint32_t opened,attempted,started,stopped,flushed,pending,closed,unknown;
 uint32_t opens,appends,starts,stops,releases,closes;
} Data;
_Static_assert(sizeof(Data)<=4096,"IZDOOM native state");
static uint64_t call(IzNativeApp2*x,unsigned f,uint64_t a,uint64_t b,uint64_t c,uint64_t d){return x->audio->functions[f](a,b,c,d);}
static uint64_t address(const void*p){return (uint64_t)(uintptr_t)p;}
static uint64_t descriptor(Data*d,unsigned i){return address(d->metadata+1024+i*64);}
static int valid(const uint32_t*w){
 return w[0]==0x44413138&&w[1]==1&&w[2]==PRIVATE_BYTES&&w[3]==PCM_BYTES&&w[4]==48000&&w[5]==2&&w[6]==FRAMES&&w[7]==256&&w[33]==31250000&&w[34]==32&&w[35]==FRAMES*32u&&w[37]==8&&w[38]==PCM_OFFSET&&w[39]==24000&&w[40]<=32768&&w[41]==0&&w[42]<=1&&w[43]==0x44313847&&w[8]<=1&&w[9]<=6&&w[11]<=2&&w[14]<=1;
}
static uint32_t cleanup(IzNativeApp2*x,Data*d){
 if(d->closed)return 0;
 if(d->unknown||(d->attempted&&!d->opened))return FAIL;
 if(d->opened){
  if(!d->stopped){if(call(x,IZ_AUDIO_STOP,address(d->metadata),0,0,0))return FAIL;d->stopped=1;d->stops++;}
  if(!d->flushed){if(call(x,IZ_AUDIO_FLUSH,address(d->metadata),0,0,0)>1)return FAIL;d->flushed=1;}
  uint64_t until=x->ticks(&x->base)+x->base.tick_hz*2;
  while(d->pending){
   uint64_t p=call(x,IZ_AUDIO_RELEASED,address(d->metadata),0,0,0);
   if(!p){if(x->ticks(&x->base)>=until)return FAIL;x->sleep(&x->base,5000000);continue;}
   uint64_t first=descriptor(d,0);if(p<first||(p-first)%64||(p-first)/64>=32)return d->unknown=1,FAIL;
   uint32_t bit=1u<<((p-first)/64);if(!(d->pending&bit))return d->unknown=1,FAIL;d->pending&=~bit;d->releases++;
  }
  if(call(x,IZ_AUDIO_COUNT,address(d->metadata),0,0,0))return FAIL;
  call(x,IZ_AUDIO_CLOSE,address(d->metadata),0,0,0);d->opened=0;d->closes++;
 }
 d->closed=1;return 0;
}
static uint32_t prepare(IzNativeApp2*x,Data*d,volatile uint32_t*w){
 if(w[9]!=1||w[8]||w[12]||w[10]!=FRAMES||w[23]!=8192||w[36]!=BANK_WORDS)return FAIL;
 call(x,IZ_AUDIO_INITIALIZE,address(d->metadata+768),0,0,0);
 *(uint32_t*)(d->metadata+768)=48000;*(uint16_t*)(d->metadata+772)=2;
 d->attempted=1;if(call(x,IZ_AUDIO_OPEN,address(d->metadata),address(d->metadata+768),0,0))return FAIL;d->opened=1;d->opens++;
 if(call(x,IZ_AUDIO_RATE,address(d->metadata),0,0,0)!=48000||call(x,IZ_AUDIO_CHANNELS,address(d->metadata),0,0,0)!=2||call(x,IZ_AUDIO_FORMAT,address(d->metadata),0,0,0)!=2)return FAIL;
 for(unsigned i=0;i<32;i++){
  call(x,IZ_AUDIO_BUFFER,descriptor(d,i),address(x->private_memory)+PCM_OFFSET,PCM_BYTES,PCM_BYTES);
  uint64_t accepted=call(x,IZ_AUDIO_APPEND,address(d->metadata),descriptor(d,i),0,0);
  if(accepted!=1){if(accepted>1)d->unknown=1;return FAIL;}d->pending|=1u<<i;d->appends++;
 }
 w[14]=1;return 0;
}
__attribute__((section(".text.iz_app"))) uint32_t iz_app(IzNativeApp*app,uint32_t event){
 if(!app||app->magic!=0x414e5a49||app->version!=2||app->bytes!=sizeof(IzNativeApp2)||app->data_bytes<sizeof(Data)||app->controls_bytes<1024)return FAIL;
 IzNativeApp2*x=(void*)app;Data*d=app->data;volatile uint32_t*w=(volatile uint32_t*)app->controls+FIRST;
 if(!x->audio||x->audio->version!=1||x->audio->bytes!=sizeof(IzNativeAudio)||!x->ticks||!x->sleep||!x->private_memory||x->private_bytes!=PRIVATE_BYTES)return FAIL;
 if(event==IZ_NATIVE_STOP)return cleanup(x,d);
 if(event==IZ_NATIVE_INIT)return valid((const uint32_t*)w)?0:FAIL;
 if(event!=IZ_NATIVE_FRAME||!valid((const uint32_t*)w)||d->closed)return FAIL;
 if(w[12]||w[13]||w[9]==4||w[9]==5){app->request_exit(app);return 0;}
 if(!d->opened&&w[9]==1)return prepare(x,d,w);
 if(d->opened&&!d->started&&w[9]==2&&w[11]==2){
  if(w[14]!=1||w[8]||w[12]||w[23]!=8192)return FAIL;
  w[15]=w[17];w[16]=w[18];if(call(x,IZ_AUDIO_START,address(d->metadata),0,0,0))return FAIL;
  d->started=1;d->starts++;w[8]=1;
 }
 return 0;
}
