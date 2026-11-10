#include "jit-shadow.h"
#include "execution-internal.h"
#include "gpu-profile.h"
extern void*memset(void*,int,size_t);
extern void*memcpy(void*,const void*,size_t);
#define LIMIT (UINT64_C(1)<<39)
enum {SOURCE=1,PHYSICAL_COUNT,PHYSICAL_VALUE,PROPERTIES,ALLOCATE,MAP,SHADOW,CLEAN_SHADOW,UNMAP,FREE};
static int range(uint64_t p,uint64_t n){return p&&!(p&4095)&&n&&!(n&4095)&&p<LIMIT&&n<=LIMIT-p;}
static int overlap(IzPs2MemorySpan a,IzPs2MemorySpan b){return a.base<b.base+b.bytes&&b.base<a.base+a.bytes;}
static uint32_t get32(const uint8_t*p){uint32_t n;memcpy(&n,p,4);return n;}
static uint64_t get64(const uint8_t*p){uint64_t n;memcpy(&n,p,8);return n;}
static uint32_t lock(IzPs2Shadow*s){uint32_t expected=0;return __atomic_compare_exchange_n(&s->busy,&expected,1,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED)?0:0xf219;}
static void unlock(IzPs2Shadow*s){__atomic_store_n(&s->busy,0,__ATOMIC_RELEASE);}
static uint32_t retain(IzPs2Shadow*s,uint32_t error){s->error=error?error:0xf201;s->state=IZ_PS2_SHADOW_RETAINED;s->pending=1;return s->error;}
static void failed(IzPs2Shadow*s,uint32_t error){s->error=error;s->state=IZ_PS2_SHADOW_FAILED;}
uint32_t iz_ps2_shadow_select(const uint8_t b[520],IzPs2ShadowSelection*out){
 if(!b||!out)return 0xf202;uint32_t count=get32(b),heaps=get32(b+260);if(!count||count>32||!heaps||heaps>16)return 0xf202;IzPs2ShadowSelection best={0};
 for(uint32_t i=0;i<count;i++){uint32_t flags=get32(b+4+i*8),heap=get32(b+8+i*8);if(heap>=heaps)return 0xf202;uint64_t bytes=get64(b+264+heap*16);if((flags&10)!=10||bytes<IZ_PS2_JIT_BYTES)continue;
  uint32_t coherent=!!(flags&4);if(!best.heap_bytes||bytes>best.heap_bytes||(bytes==best.heap_bytes&&coherent&&!best.coherent))best=(IzPs2ShadowSelection){i,flags,heap,coherent,bytes};
 }if(!best.heap_bytes)return 0xf203;*out=best;return 0;
}
uint32_t iz_ps2_shadow_allocation(uint32_t index,uint8_t out[32]){if(!out||index>=32)return 0xf202;uint32_t type=5;uint64_t bytes=IZ_PS2_JIT_BYTES;memset(out,0,32);memcpy(out,&type,4);memcpy(out+16,&bytes,8);memcpy(out+24,&index,4);return 0;}
uint32_t iz_ps2_shadow_begin(IzPs2Shadow*s,const IzPs2ShadowOps*ops,const IzNativeEnvironment*e,const IzNativeGraphics*g,const IzPs2Memory*m){
 if(g&&g->function_count!=IZ_GFX_COUNT)return 0xf202;
 if(!s||!ops||!e||!g||!m||!ops->identity||!ops->query||!ops->physical||!ops->properties||!ops->allocate||!ops->map||!ops->unmap||!ops->release||e->version!=1||e->bytes!=sizeof *e||e->sdk_bytes!=IZ_PS2_SDK_BYTES||!e->application_pid||e->application_pid>=(UINT64_C(1)<<32)||g->version!=1||g->bytes!=sizeof *g||!range(g->vulkan,IZ_PS2_VULKAN_BYTES)||!g->instance||g->instance>=LIMIT||!g->physical||g->physical>=LIMIT||!g->device||g->device>=LIMIT||m->busy||m->pending||m->entered||m->state!=IZ_PS2_MEMORY_READY||m->pid!=e->application_pid||m->count!=m->wanted||m->count<4||m->count>5||m->protected_count!=4||m->protected[0].base!=e->sdk||m->protected[0].bytes!=e->sdk_bytes)return 0xf202;
 IzPs2MemorySpan protected[10];uint32_t n=0;for(unsigned i=0;i<4;i++)protected[n++]=m->protected[i];for(unsigned i=0;i<m->count;i++)protected[n++]=m->blocks[i];protected[n++]=(IzPs2MemorySpan){g->vulkan,IZ_PS2_VULKAN_BYTES};
 if(m->blocks[IZ_PS2_JIT_SOURCE].bytes!=IZ_PS2_JIT_BYTES)return 0xf202;
 for(unsigned i=0;i<n;i++){if(!range(protected[i].base,protected[i].bytes))return 0xf202;for(unsigned j=0;j<i;j++)if(overlap(protected[i],protected[j]))return 0xf202;}
 uint32_t rc=lock(s);if(rc)return rc;if(s->memory||s->mapped||s->pending||s->entered||s->state==IZ_PS2_SHADOW_PREPARING||s->state==IZ_PS2_SHADOW_CLOSING){unlock(s);return 0xf219;}
 s->state=IZ_PS2_SHADOW_PREPARING;s->error=s->pending=s->entered=s->map_known=s->regions=0;s->phase=SOURCE;s->pid=e->application_pid;s->instance=g->instance;s->physical=g->physical;s->device=g->device;s->memory=s->mapped=s->units=s->allocation_calls=s->map_calls=s->unmap_calls=s->free_calls=0;s->physical_count=0;s->ops=*ops;s->selection=(IzPs2ShadowSelection){0};s->source=m->blocks[IZ_PS2_JIT_SOURCE];s->walk=s->source.base;s->end=s->walk+s->source.bytes;s->protected_count=n;memcpy(s->protected,protected,n*sizeof *protected);unlock(s);return 0;
}
static void walk_shadow(IzPs2Shadow*s,uint32_t phase){s->walk=s->mapped;s->end=s->mapped+IZ_PS2_JIT_BYTES;s->regions=0;s->phase=phase;}
static uint32_t query(IzPs2Shadow*s,uint32_t attributes){
 if(++s->regions>32768)return retain(s,0xf204);IzPs2MemoryRegion r={0};uint32_t rc=s->ops.query(s->ops.owner,s->walk,&r);if(rc||!range(r.base,r.bytes)||r.base>s->walk||r.base+r.bytes<=s->walk||r.type!=5||r.permission!=3||r.attributes!=attributes||r.ipc_refs||r.device_refs)return retain(s,0xf204);
 uint64_t end=r.base+r.bytes;s->walk=end<s->end?end:s->end;return 0;
}
static uint32_t advance(IzPs2Shadow*s){
 uint64_t pid=0;uint32_t rc=s->ops.identity(s->ops.owner,&pid);if(rc||pid!=s->pid)return retain(s,0xf205);
 switch(s->phase){
 case SOURCE:rc=query(s,0);if(rc)return rc;if(s->walk==s->end)s->phase=PHYSICAL_COUNT;break;
 case PHYSICAL_COUNT:s->physical_count=0;rc=s->ops.physical(s->ops.owner,s->instance,&s->physical_count,0);if(rc||s->physical_count!=1)failed(s,0xf206);else s->phase=PHYSICAL_VALUE;break;
 case PHYSICAL_VALUE:{uint64_t physical=0;s->physical_count=1;rc=s->ops.physical(s->ops.owner,s->instance,&s->physical_count,&physical);if(rc||s->physical_count!=1||physical!=s->physical)failed(s,0xf206);else s->phase=PROPERTIES;break;}
 case PROPERTIES:{_Alignas(8) uint8_t props[520];memset(props,0,sizeof props);rc=s->ops.properties(s->ops.owner,s->physical,props);if(rc){failed(s,0xf207);break;}rc=iz_ps2_shadow_select(props,&s->selection);if(rc||s->selection.memory_type!=2||s->selection.flags!=15||s->selection.heap||!s->selection.coherent){failed(s,0xf207);break;}s->phase=ALLOCATE;break;}
 case ALLOCATE:{_Alignas(8) uint8_t info[32];iz_ps2_shadow_allocation(s->selection.memory_type,info);s->pending=1;s->allocation_calls++;rc=s->ops.allocate(s->ops.owner,s->device,info,&s->memory);if(rc){if((rc==(uint32_t)-1||rc==(uint32_t)-2)&&!s->memory){s->pending=0;failed(s,rc);break;}return retain(s,0xf208);}if(!s->memory)return retain(s,0xf208);s->pending=0;s->phase=MAP;break;}
 case MAP:s->pending=1;s->map_calls++;rc=s->ops.map(s->ops.owner,s->device,s->memory,IZ_PS2_JIT_BYTES,&s->mapped);if(rc){if((rc==(uint32_t)-1||rc==(uint32_t)-2||rc==(uint32_t)-5)&&!s->mapped){s->pending=0;s->error=rc;s->state=IZ_PS2_SHADOW_CLOSING;s->phase=FREE;break;}return retain(s,0xf209);}if(!range(s->mapped,IZ_PS2_JIT_BYTES))return retain(s,0xf209);s->map_known=1;
  for(unsigned i=0;i<s->protected_count;i++)if(overlap((IzPs2MemorySpan){s->mapped,IZ_PS2_JIT_BYTES},s->protected[i]))return retain(s,0xf20a);s->pending=0;walk_shadow(s,SHADOW);break;
 case SHADOW:rc=query(s,0x60);if(rc)return rc;if(s->walk==s->end)s->state=IZ_PS2_SHADOW_READY;break;
 case CLEAN_SHADOW:rc=query(s,0x60);if(rc)return rc;if(s->walk==s->end)s->phase=UNMAP;break;
 case UNMAP:s->pending=1;s->unmap_calls++;rc=s->ops.unmap(s->ops.owner,s->device,s->memory);if(rc)return retain(s,0xf20b);s->pending=0;s->map_known=0;s->mapped=0;s->phase=FREE;break;
 case FREE:s->pending=1;s->free_calls++;rc=s->ops.release(s->ops.owner,s->device,s->memory);if(rc)return retain(s,0xf20c);s->pending=0;s->memory=0;s->state=s->error?IZ_PS2_SHADOW_FAILED:IZ_PS2_SHADOW_CLOSED;break;
 default:return retain(s,0xf20d);
 }return 0;
}
uint32_t iz_ps2_shadow_step(IzPs2Shadow*s,uint32_t budget,uint32_t*done){
 if(!s||!done||!budget||budget>64)return 0xf202;*done=0;uint32_t rc=lock(s);if(rc)return rc;if(s->state==IZ_PS2_SHADOW_IDLE||s->state==IZ_PS2_SHADOW_ENTERED){*done=1;unlock(s);return 0xf202;}
 while((s->state==IZ_PS2_SHADOW_PREPARING||s->state==IZ_PS2_SHADOW_CLOSING)&&budget--){rc=advance(s);s->units++;if(rc)break;}
 if(s->state!=IZ_PS2_SHADOW_PREPARING&&s->state!=IZ_PS2_SHADOW_CLOSING){*done=1;rc=s->error;}unlock(s);return rc;
}
uint32_t iz_ps2_shadow_cancel(IzPs2Shadow*s,uint32_t reason){
 if(!s)return 0xf202;uint32_t rc=lock(s);if(rc)return rc;if(s->pending||s->entered||s->state==IZ_PS2_SHADOW_RETAINED)rc=retain(s,s->error?s->error:0xf210);
 else if(s->state!=IZ_PS2_SHADOW_IDLE&&s->state!=IZ_PS2_SHADOW_CLOSED&&s->state!=IZ_PS2_SHADOW_FAILED&&s->state!=IZ_PS2_SHADOW_CLOSING){s->error=reason;s->state=IZ_PS2_SHADOW_CLOSING;if(s->map_known)walk_shadow(s,CLEAN_SHADOW);else if(s->memory)s->phase=FREE;else s->state=reason?IZ_PS2_SHADOW_FAILED:IZ_PS2_SHADOW_CLOSED;}
 unlock(s);return rc;
}
uint32_t iz_ps2_shadow_entered(IzPs2Shadow*s){if(!s)return 0xf202;uint32_t rc=lock(s);if(rc)return rc;if(s->state!=IZ_PS2_SHADOW_READY||s->pending||s->entered||!s->memory||!s->mapped||!s->map_known)rc=0xf211;else{s->entered=1;s->state=IZ_PS2_SHADOW_ENTERED;}unlock(s);return rc;}
#ifdef IZ_PS2_EXECUTION
uint32_t iz_ps2_shadow_retire(IzPs2Shadow*s,const IzPs2Execution*e){if(!s)return 0xf202;uint32_t rc=lock(s);if(rc)return rc;
 if(s->state!=IZ_PS2_SHADOW_ENTERED||!s->entered||s->pending||!iz_ps2_execution_release_match(e,s,IZ_PS2_RETIRE_SHADOW))rc=0xf212;
 else{s->error=0;s->state=IZ_PS2_SHADOW_CLOSING;walk_shadow(s,CLEAN_SHADOW);}unlock(s);return rc;
}
#endif
