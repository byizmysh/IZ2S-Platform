#include "memory.h"
#include "execution-internal.h"
#define PAGE UINT64_C(4096)
#define LIMIT (UINT64_C(1)<<39)
enum {ALLOCATE=1,QUERY,CLEAN_BEGIN,CLEAN_QUERY,FREE};
static int range(uint64_t p,uint64_t n){return p&&!(p&(PAGE-1))&&n&&!(n&(PAGE-1))&&p<LIMIT&&n<=LIMIT-p;}
static int overlap(IzPs2MemorySpan a,IzPs2MemorySpan b){return a.base<b.base+b.bytes&&b.base<a.base+a.bytes;}
static uint32_t lock(IzPs2Memory*m){uint32_t expected=0;return __atomic_compare_exchange_n(&m->busy,&expected,1,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED)?0:0xec19;}
static void unlock(IzPs2Memory*m){__atomic_store_n(&m->busy,0,__ATOMIC_RELEASE);}
static uint32_t retained(IzPs2Memory*m,uint32_t rc){m->error=rc?rc:0xec01;m->pending=1;m->state=IZ_PS2_MEMORY_RETAINED;return m->error;}
static uint64_t block_bytes(unsigned index){switch(index){case IZ_PS2_MAIN:return IZ_PS2_MAIN_BYTES;case IZ_PS2_JIT_SOURCE:return IZ_PS2_JIT_BYTES;case IZ_PS2_ENTRY_STACK:return IZ_PS2_STACK_BYTES+8192;default:return PAGE;}}
static void clean(IzPs2Memory*m,uint32_t error){m->error=error;m->state=IZ_PS2_MEMORY_CLOSING;m->phase=CLEAN_BEGIN;m->index=m->count;}
static int admitted(IzPs2Memory*m,IzPs2MemorySpan candidate){
 if(!range(candidate.base,candidate.bytes))return 0;
 for(unsigned i=0;i<m->count;i++)if(overlap(candidate,m->blocks[i]))return 0;
 for(unsigned i=0;i<m->protected_count;i++)if(overlap(candidate,m->protected[i]))return 0;return 1;
}
uint32_t iz_ps2_memory_begin(IzPs2Memory*m,const IzPs2MemoryOps*ops,const IzNativeEnvironment*e,uint64_t parent,uint64_t parent_bytes,uint32_t hwgs){
 if(!m||!ops||!e||!ops->identity||!ops->allocate||!ops->query||!ops->release||hwgs>1||e->version!=1||e->bytes!=sizeof *e||e->sdk_bytes!=IZ_PS2_SDK_BYTES||!range(e->sdk,e->sdk_bytes)||!range(parent,parent_bytes)||!e->application_pid||e->application_pid>=(UINT64_C(1)<<32))return 0xec02;
 IzPs2MemorySpan sdk={e->sdk,e->sdk_bytes},p={parent,parent_bytes};if(overlap(sdk,p))return 0xec02;
 uint32_t rc=lock(m);if(rc)return rc;if(m->count||m->pending||m->entered||m->state==IZ_PS2_MEMORY_PREPARING||m->state==IZ_PS2_MEMORY_CLOSING){unlock(m);return 0xec19;}
 m->state=IZ_PS2_MEMORY_PREPARING;m->error=0;m->pending=0;m->phase=ALLOCATE;m->count=0;m->wanted=1;m->hwgs=hwgs;m->index=0;m->walk_regions=0;m->entered=0;m->protected_count=2;m->pid=e->application_pid;m->walk_at=m->walk_end=m->units=m->allocation_calls=m->free_calls=0;m->ops=*ops;m->protected[0]=sdk;m->protected[1]=p;for(unsigned i=0;i<IZ_PS2_MEMORY_BLOCKS;i++)m->blocks[i]=(IzPs2MemorySpan){0,0};unlock(m);return 0;
}
uint32_t iz_ps2_memory_after_image(IzPs2Memory*m,const IzNativeImage*image){
 if(!m||!image)return 0xec03;uint32_t rc=lock(m);if(rc)return rc;
 if(m->state!=IZ_PS2_MEMORY_READY||m->pending||m->count!=1||m->wanted!=1||image->version!=1||image->bytes!=sizeof *image||!image->token||image->code_bytes!=((IZ_PS2_CODE_BYTES+4095)&~4095ull)||image->data_bytes!=((IZ_PS2_DATA_BYTES+4095)&~4095ull)||(uintptr_t)image->entry!=(uintptr_t)image->code||(uintptr_t)image->spawn!=(uintptr_t)image->code+120)rc=0xec03;
 else{IzPs2MemorySpan code={(uintptr_t)image->code,image->code_bytes},data={(uintptr_t)image->data,image->data_bytes};if(!admitted(m,code)||!admitted(m,data)||overlap(code,data))rc=0xec03;
  else{m->protected[2]=code;m->protected[3]=data;m->protected_count=4;m->wanted=m->hwgs?5:4;m->state=IZ_PS2_MEMORY_PREPARING;m->phase=ALLOCATE;}}
 unlock(m);return rc;
}
static uint32_t query(IzPs2Memory*m){
 if(++m->walk_regions>1024)return retained(m,0xec04);IzPs2MemoryRegion r={0};uint32_t rc=m->ops.query(m->ops.owner,m->walk_at,&r);
 if(rc||!range(r.base,r.bytes)||r.base>m->walk_at||r.base+r.bytes<=m->walk_at||r.type!=5||r.attributes||r.permission!=3||r.ipc_refs||r.device_refs)return retained(m,0xec04);
 uint64_t end=r.base+r.bytes;m->walk_at=end<m->walk_end?end:m->walk_end;return 0;
}
static uint32_t advance(IzPs2Memory*m){
 uint64_t pid=0;uint32_t rc=m->ops.identity(m->ops.owner,&pid);if(rc||pid!=m->pid)return retained(m,0xec05);
 switch(m->phase){
 case ALLOCATE:{uint64_t address=0,bytes=block_bytes(m->count);m->pending=1;m->allocation_calls++;rc=m->ops.allocate(m->ops.owner,bytes,&address);
  if(rc==12&&!address){m->pending=0;clean(m,12);break;}
  IzPs2MemorySpan candidate={address,bytes};if(rc||!admitted(m,candidate))return retained(m,0xec06);
  m->blocks[m->count++]=candidate;m->pending=0;m->walk_at=address;m->walk_end=address+bytes;m->walk_regions=0;m->phase=QUERY;break;}
 case QUERY:rc=query(m);if(rc)return rc;if(m->walk_at==m->walk_end){if(m->count==m->wanted)m->state=IZ_PS2_MEMORY_READY;else m->phase=ALLOCATE;}break;
 case CLEAN_BEGIN:if(!m->index){m->state=m->error?IZ_PS2_MEMORY_FAILED:IZ_PS2_MEMORY_CLOSED;break;}m->index--;if(!m->index&&m->entered&&!m->blocks[0].base&&!m->blocks[0].bytes){m->count--;break;}m->walk_at=m->blocks[m->index].base;m->walk_end=m->walk_at+m->blocks[m->index].bytes;m->walk_regions=0;m->phase=CLEAN_QUERY;break;
 case CLEAN_QUERY:rc=query(m);if(rc)return rc;if(m->walk_at==m->walk_end)m->phase=FREE;break;
 case FREE:m->pending=1;m->free_calls++;rc=m->ops.release(m->ops.owner,m->blocks[m->index].base);if(rc)return retained(m,0xec07);m->pending=0;m->blocks[m->index]=(IzPs2MemorySpan){0,0};m->count--;m->phase=CLEAN_BEGIN;break;
 default:return retained(m,0xec08);
 }return 0;
}
uint32_t iz_ps2_memory_step(IzPs2Memory*m,uint32_t budget,uint32_t*done){
 if(!m||!done||!budget||budget>64)return 0xec09;*done=0;uint32_t rc=lock(m);if(rc)return rc;
 if(m->state==IZ_PS2_MEMORY_IDLE||m->state>=IZ_PS2_MEMORY_ENTERED){*done=1;unlock(m);return 0xec09;}
 while((m->state==IZ_PS2_MEMORY_PREPARING||m->state==IZ_PS2_MEMORY_CLOSING)&&budget--){rc=advance(m);m->units++;if(rc)break;}
 if(m->state==IZ_PS2_MEMORY_READY||m->state==IZ_PS2_MEMORY_CLOSED||m->state==IZ_PS2_MEMORY_FAILED||m->state==IZ_PS2_MEMORY_RETAINED){*done=1;rc=m->error;}
 unlock(m);return rc;
}
uint32_t iz_ps2_memory_cancel(IzPs2Memory*m,uint32_t error){
 if(!m)return 0xec09;uint32_t rc=lock(m);if(rc)return rc;
 if(m->pending||m->entered||m->state==IZ_PS2_MEMORY_RETAINED)rc=retained(m,m->error?m->error:0xec10);
 else if(m->state==IZ_PS2_MEMORY_CLOSED||m->state==IZ_PS2_MEMORY_FAILED||m->state==IZ_PS2_MEMORY_IDLE)rc=0;
 else if(m->state!=IZ_PS2_MEMORY_CLOSING)clean(m,error);
 unlock(m);return rc;
}
uint32_t iz_ps2_memory_entered(IzPs2Memory*m){
 if(!m)return 0xec09;uint32_t rc=lock(m);if(rc)return rc;
 if(m->state!=IZ_PS2_MEMORY_READY||m->pending||m->count!=m->wanted||m->wanted<4||m->entered)rc=0xec11;
 else{m->entered=1;m->state=IZ_PS2_MEMORY_ENTERED;}unlock(m);return rc;
}
#ifdef IZ_PS2_EXECUTION
uint32_t iz_ps2_memory_retire(IzPs2Memory*m,const IzPs2Execution*e){
 if(!m)return 0xec09;uint32_t rc=lock(m);if(rc)return rc;
 if(m->state!=IZ_PS2_MEMORY_ENTERED||!m->entered||m->pending||!iz_ps2_execution_release_match(e,m,IZ_PS2_RETIRE_MEMORY))rc=0xec12;
 else{m->blocks[IZ_PS2_MAIN]=(IzPs2MemorySpan){0,0};clean(m,0);}
 unlock(m);return rc;
}
#endif
