#include "native-app.h"
#define R451_MAGIC UINT32_C(0x52343531)
#define R451_PATH "cpu-accounting-r451-closed.bin"
#define R451_ITERATIONS UINT64_C(1048576)
typedef struct {
 uint32_t magic,version,bytes,error,result[4];
 uint64_t cpu[4],wall[4],tick_hz,iterations,sdk,flags;
} R451Receipt;
_Static_assert(sizeof(R451Receipt)==128,"Bounded closed receipt");
typedef struct {R451Receipt r;uint32_t initialized,write_attempted,write_result,exit_requested;} R451State;
typedef uint32_t (*R451Info)(uint64_t*,uint32_t,uint32_t,uint64_t);
#ifdef R451_MODEL
extern uint32_t r451_model_info(uint64_t*,uint32_t,uint32_t,uint64_t);
#endif
static int equal(const unsigned char*a,const unsigned char*b,unsigned n){for(unsigned i=0;i<n;i++)if(a[i]!=b[i])return 0;return 1;}
static uint32_t sample(IzNativeApp*a,IzNativeApp2*b,R451State*s,unsigned i,R451Info info){
 uint64_t value=0;uint32_t rc=info(&value,25,UINT32_C(0xffff8000),UINT64_MAX);
 s->r.result[i]=rc;s->r.wall[i]=b->ticks(a);
 if(rc)return 0xe791;
 s->r.cpu[i]=value;
 if(i&&(value<s->r.cpu[i-1]||s->r.wall[i]<s->r.wall[i-1]))return 0xe792;
 return 0;
}
/* Every sample concerns this callback's current native thread, not the game's
 * EE/GS workers. No borrowed thread object, worker, event or heap is acquired. */
__attribute__((section(".text.iz_app"))) uint32_t iz_app(IzNativeApp*a,uint32_t event){
 if(event==IZ_NATIVE_STOP){
  if(a&&a->data&&a->data_bytes>=sizeof(R451State)){
   R451State*s=a->data;return s->initialized&&s->write_attempted?s->write_result:0;
  }
  return 0;
 }
 if(!a||a->magic!=0x414e5a49||a->version!=6||a->bytes!=sizeof(IzNativeApp6)||!a->data||a->data_bytes<sizeof(R451State)||!a->write_file||!a->request_exit)return 0xe790;
 R451State*s=a->data;IzNativeApp6*x=(void*)a;IzNativeApp2*b=&x->base.base.base.base;
 if(event==IZ_NATIVE_FRAME)return s->initialized&&s->exit_requested?0:0xe790;
 if(event!=IZ_NATIVE_INIT||s->initialized)return 0xe790;
 const IzNativeEnvironment*e=x->base.environment;
 if(!e||e->version!=1||e->bytes!=sizeof *e||!e->sdk||e->sdk%4096||e->sdk>=(UINT64_C(1)<<39)||e->sdk_bytes<0x1584c4||e->sdk_bytes>(UINT64_C(1)<<39)-e->sdk||e->tick_hz!=31250000||a->tick_hz!=e->tick_hz||!b->ticks||!b->sleep)return 0xe790;
 *s=(R451State){.r={.magic=R451_MAGIC,.version=1,.bytes=sizeof(R451Receipt),.tick_hz=e->tick_hz,.sdk=e->sdk},.initialized=1};
 static const unsigned char code[24]={0xe0,0x0f,0x1f,0xf8,0x21,0x05,0x00,0xd4,0xe2,0x03,0x40,0xf9,0x41,0x00,0x00,0xf9,0xff,0x43,0x00,0x91,0xc0,0x03,0x5f,0xd6};
 if(!equal((const void*)(uintptr_t)(e->sdk+0x1584ac),code,sizeof code))s->r.error=0xe793;
 else{
  s->r.flags|=1;
#ifdef R451_MODEL
  R451Info info=r451_model_info;
#else
  R451Info info=(R451Info)(uintptr_t)(e->sdk+0x1584ac);
#endif
  s->r.error=sample(a,b,s,0,info);
  if(!s->r.error){
   uint64_t tick=s->r.wall[0],target=e->tick_hz/50;
   while(tick>=s->r.wall[0]&&tick-s->r.wall[0]<target&&s->r.iterations<R451_ITERATIONS){
    s->r.iterations++;tick=b->ticks(a);
   }
   if(tick<s->r.wall[0]||tick-s->r.wall[0]<target)s->r.error=0xe794;
   else{s->r.flags|=4;s->r.error=sample(a,b,s,1,info);}
  }
  if(!s->r.error)s->r.error=sample(a,b,s,2,info);
  if(!s->r.error){b->sleep(a,20000000);s->r.error=sample(a,b,s,3,info);}
  if(!s->r.error)s->r.flags|=2|8;
 }
 /* No module-owned resource remains. The public file provider owns its
  * transaction. A returned write error is preserved, never retried. */
 s->r.flags|=16;s->write_attempted=1;
 s->write_result=a->write_file(a,R451_PATH,&s->r,sizeof s->r);
 if(s->write_result)return s->write_result;
 s->exit_requested=1;a->request_exit(a);return 0;
}
