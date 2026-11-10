#include "abi.h"
typedef struct {
 HeavyContext base;
 uint32_t service,loops;
 uint64_t send,pid,source,rx,pages,mapped,patches,executions,first,last,ro_pid;
 uint64_t begin,end,transport,operation_result;
} Probe;
_Static_assert(sizeof(Probe)==384,"JIT probe ABI");
static void clear(void*p,size_t n){uint8_t*b=p;while(n--)*b++=0;}
static uint32_t request(Probe*c,uint32_t command,const uint64_t*input,unsigned count,uint8_t*buffer){
 clear(buffer,4096);uint32_t*p=(void*)buffer;
 p[0]=4;p[1]=0x80000000u+(32+count*8)/4;p[2]=3;p[5]=0xffff8001u;
 p[8]=0x49434653;p[10]=command;
 uint64_t*words=(void*)(buffer+48);for(unsigned i=0;i<count;i++)words[i]=input[i];
 c->transport=((uint32_t(*)(void*,uint64_t,uint32_t))(uintptr_t)c->send)(buffer,4096,c->service);
 if(c->transport)return 1;
 if(!(p[1]&0x80000000u)||p[2]!=1||p[8]!=0x4f434653)return 2;
 if(((uint64_t)p[3]|((uint64_t)p[4]<<32))!=c->ro_pid)return 3;
 c->operation_result=p[10];if(p[10])return 4;
 return 0;
}
uint32_t heavy_main(HeavyContext*base){
 Probe*c=(void*)base;
 if(!c||base->magic!=HEAVY_MAGIC||!c->send||!c->pid||!c->ro_pid||c->pages!=1||!c->loops||c->loops>10000)return 1;
 _Alignas(4096) uint8_t message[4096];uint64_t in[8]={c->pid,c->source,c->rx,4096};
 base->status=1;base->phase=1;base->error=0;
 c->mapped=2; /* Preserve the source when a transport outcome is uncertain. */
 uint32_t error=request(c,0x524f0012,in,4,message);
 if(error){
  if(error==4){uint64_t*out=(void*)(message+48);if(out[3]||(out[4]&&!out[5]))c->mapped=0;}
  base->status=3;base->error=10+error;return base->error;
 }
 c->mapped=1;base->phase=2;
 c->begin=((uint64_t(*)(void))(uintptr_t)base->tick)();
 for(uint32_t i=1;i<=c->loops;i++){
  const volatile uint64_t*old=(void*)(uintptr_t)c->rx;
  uint32_t value=(0x5aa5u^i)&65535;
  in[0]=c->pid;in[1]=c->rx;in[2]=16;in[3]=0;in[4]=old[0];in[5]=old[1];
  in[6]=0xd50324dfull|((uint64_t)(0x52800000u|(value<<5))<<32);
  in[7]=0xd65f03c0ull|(0xd503201full<<32);
  error=request(c,0x524f0014,in,8,message);if(error){base->error=20+error;break;}
  uint64_t*out=(void*)(message+48);
  if(out[6]!=1||out[7]!=1||out[8]){base->error=25;break;}
  c->patches++;
  uint32_t result=((uint32_t(*)(void))(uintptr_t)c->rx)();
  if(result!=value){base->error=26;break;}
  if(!c->executions)c->first=result;c->last=result;c->executions++;
 }
 c->end=((uint64_t(*)(void))(uintptr_t)base->tick)();base->phase=3;
 in[0]=c->pid;in[1]=c->source;in[2]=c->rx;in[3]=4096;
 error=request(c,0x524f0013,in,4,message);
 if(error){base->error=30+error;base->status=3;return base->error;}
 c->mapped=0;base->phase=4;base->status=base->error?3:2;return base->error;
}
