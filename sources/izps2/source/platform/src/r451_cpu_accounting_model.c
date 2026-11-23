#include "native-app.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
extern uint32_t iz_app(IzNativeApp*,uint32_t);
typedef struct{uint32_t magic,version,bytes,error,result[4];uint64_t cpu[4],wall[4],tick_hz,iterations,sdk,flags;} Receipt;
static unsigned char sdk[0x159000] __attribute__((aligned(4096)));
static unsigned char state[4096] __attribute__((aligned(64)));
static uint64_t clock_ticks,slept_ticks;static unsigned fault,queries,writes,exits,sleeps;static Receipt disk;
static uint64_t ticks(IzNativeApp*a){(void)a;if(fault!=5)clock_ticks+=100;return clock_ticks;}
static void sleep_call(IzNativeApp*a,uint64_t ns){(void)a;assert(ns==20000000);sleeps++;clock_ticks+=625000;slept_ticks+=625000;}
uint32_t r451_model_info(uint64_t*out,uint32_t id,uint32_t h,uint64_t sub){
 assert(id==25&&h==0xffff8000u&&sub==UINT64_MAX);queries++;
 *out=clock_ticks-slept_ticks;
 if((fault==2&&queries==1)||(fault==3&&queries==3)){*out=UINT64_MAX;return 519;}
 if(fault==4&&queries==2)*out=0;
 return 0;
}
static uint32_t write_call(IzNativeApp*a,const char*n,const void*p,uint32_t bytes){
 (void)a;assert(!strcmp(n,"cpu-accounting-r451-closed.bin")&&bytes==128);writes++;disk=*(const Receipt*)p;
 assert(disk.magic==0x52343531&&disk.bytes==128&&(disk.flags&16));return fault==6?0x456:0;
}
static void exit_call(IzNativeApp*a){(void)a;assert(writes==1);exits++;}
int main(void){
 static const unsigned char code[24]={0xe0,0x0f,0x1f,0xf8,0x21,0x05,0x00,0xd4,0xe2,0x03,0x40,0xf9,0x41,0x00,0x00,0xf9,0xff,0x43,0x00,0x91,0xc0,0x03,0x5f,0xd6};
 for(unsigned k=0;k<10;k++){
  fault=k;queries=writes=exits=sleeps=0;clock_ticks=10000;slept_ticks=0;memset(state,0,sizeof state);memset(&disk,0,sizeof disk);memcpy(sdk+0x1584ac,code,24);
  IzNativeEnvironment e={.version=1,.bytes=sizeof e,.sdk=(uint64_t)(uintptr_t)sdk,.sdk_bytes=sizeof sdk,.tick_hz=31250000};
  IzNativeApp6 x={0};IzNativeApp*a=(void*)&x;IzNativeApp2*b=&x.base.base.base.base;
  a->magic=0x414e5a49;a->version=6;a->bytes=sizeof x;a->data=state;a->data_bytes=sizeof state;a->tick_hz=e.tick_hz;a->write_file=write_call;a->request_exit=exit_call;
  x.base.environment=&e;b->ticks=ticks;b->sleep=sleep_call;
  if(k==1)sdk[0x1584ac]^=1;
  if(k==7)a->version=5;
  if(k==8)e.sdk_bytes=0x1584c3;
  if(k==9)b->ticks=0;
  uint32_t rc=iz_app(a,IZ_NATIVE_INIT);
  if(k>=7){assert(rc==0xe790&&!queries&&!writes&&!exits);assert(!iz_app(a,IZ_NATIVE_STOP));continue;}
  assert(writes==1&&exits==(k==6?0u:1u)&&rc==(k==6?0x456u:0u));
  if(k==0||k==6){assert(!disk.error&&queries==4&&sleeps==1&&disk.flags==31);assert(disk.cpu[1]>disk.cpu[0]&&disk.cpu[3]-disk.cpu[2]<disk.wall[3]-disk.wall[2]);}
  if(k==1)assert(disk.error==0xe793&&!queries&&!sleeps);
  if(k==2)assert(disk.error==0xe791&&queries==1&&disk.cpu[0]==0);
  if(k==3)assert(disk.error==0xe791&&queries==3&&disk.cpu[2]==0&&!sleeps);
  if(k==4)assert(disk.error==0xe792&&queries==2&&!sleeps);
  if(k==5)assert(disk.error==0xe794&&queries==1&&disk.iterations==1048576&&!sleeps);
  unsigned q=queries,w=writes;assert(iz_app(a,IZ_NATIVE_INIT)==0xe790&&queries==q&&writes==w);
  assert(iz_app(a,IZ_NATIVE_STOP)==(k==6?0x456u:0u)&&writes==w);
  if(k!=6){assert(!iz_app(a,IZ_NATIVE_FRAME)&&queries==q&&writes==w&&exits==1);}
 }
 puts("{\"passed\":true,\"cases\":10,\"SDKCalls\":0,\"receiptBytes\":128,\"newWorkers\":0}");return 0;
}
