/* Standalone qualification of EL0 cache maintenance and warm JIT return into
 * its temporarily removed caller-owned page. No graphics, audio or filesystem. */
#include "page-jit-probe.h"
Ps1Context *ps1_context;
static intptr_t panic_frame[5];
void *memcpy(void *out,const void *in,size_t bytes){volatile uint8_t*d=out;const volatile uint8_t*s=in;while(bytes--)*d++=*s++;return out;}
void *memset(void *out,int value,size_t bytes){volatile uint8_t*d=out;while(bytes--)*d++=(uint8_t)value;return out;}
int memcmp(const void *a,const void *b,size_t bytes){const volatile uint8_t*x=a,*y=b;while(bytes--){if(*x!=*y)return *x<*y?-1:1;x++;y++;}return 0;}
void ps1_fatal(unsigned code){ps1_context->base.error=code;__builtin_longjmp(panic_frame,1);}
static uint32_t observed_send(void *message,uint64_t bytes,uint32_t service){Ps1PageJitProbe*p=(void*)ps1_context;uint32_t command=((uint32_t*)message)[10];if(command==0x524f0012)p->maps++;else if(command==0x524f0013)p->unmaps++;return ((uint32_t(*)(void*,uint64_t,uint32_t))(uintptr_t)p->sdk_send)(message,bytes,service);}
void page_probe_callback(void){
 Ps1PageJitProbe*p=(void*)ps1_context;if(!p->next_value)return;
 uint32_t instruction=0x52800000u|((uint32_t)p->next_value<<5);memcpy((void*)(uintptr_t)(p->cpu.jit_rw+20),&instruction,4);
 p->callback_error=ps1_ro_publish(&p->cpu,16,32);
 /* Never return to a JIT return address if its mapping is uncertain. */
 if(p->callback_error)ps1_fatal(200+(unsigned)p->callback_error);
}
uint32_t heavy_main(HeavyContext *base){
 Ps1PageJitProbe*p=(void*)base;Ps1Context*c=&p->cpu;
 if(!p||base->magic!=HEAVY_MAGIC||base->version!=1||!base->tick||!base->tick_hz||!p->loops||p->loops>1000||!c->service||!c->send||!c->pid||!c->ro_pid||!c->jit_source||!c->jit_rw||!c->jit_rx||c->jit_bytes!=4096||((c->jit_source|c->jit_rw|c->jit_rx)&4095)||c->pages_mapped||c->mapping_unknown)return 1;
 if(c->jit_source==c->jit_rw||c->jit_source==c->jit_rx||c->jit_rw==c->jit_rx)return 2;
 ps1_context=c;base->status=1;base->error=0;base->phase=1;c->base.flags|=PS1_FLAG_PAGE_PUBLICATION;p->sdk_send=c->send;c->send=(uintptr_t)observed_send;
 if(__builtin_setjmp(panic_frame)){base->status=3;return base->error;}
 memset((void*)(uintptr_t)c->jit_source,0,4096);memset((void*)(uintptr_t)c->jit_rw,0,4096);
 p->stage=1;ps1_ro_map();
 const uint32_t instructions[]={0xd50324df,0xa9bf7bfd,0x910003fd,0x580000b0,0xd63f0200,0x528aa940,0xa8c17bfd,0xd65f03c0};
 memcpy((void*)(uintptr_t)c->jit_rw,instructions,sizeof instructions);uintptr_t callback=(uintptr_t)page_probe_callback;memcpy((void*)(uintptr_t)(c->jit_rw+32),&callback,8);
 p->stage=2;unsigned error=ps1_ro_publish(c,0,48);if(error){p->callback_error=error;ps1_fatal(100+error);}
 p->stage=3;uint32_t(*execute)(void)=(void*)(uintptr_t)c->jit_rx;p->next_value=0;p->first=execute();p->executions++;
 if(p->first!=0x554a){c->mapping_unknown=1;ps1_fatal(270);}
 p->stage=4;base->phase=2;p->begin=((uint64_t(*)(void))(uintptr_t)base->tick)();
 for(unsigned i=1;i<=p->loops;i++){p->next_value=(0x5aa5u^i)&65535u;p->last=execute();if(p->last!=p->next_value){c->mapping_unknown=1;ps1_fatal(271);}p->executions++;}
 p->end=((uint64_t(*)(void))(uintptr_t)base->tick)();p->stage=5;base->phase=3;ps1_ro_unmap();p->stage=6;base->phase=4;base->status=2;return 0;
}
