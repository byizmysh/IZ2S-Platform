#include "sdk-memory.h"
#include "sdk-profile.h"
extern uint32_t iz_self_process_id(uint64_t*);
extern uint32_t iz_self_query(IzPs2MemoryRegion*,uint64_t);
static uint32_t identity(void*owner,uint64_t*out){(void)owner;return iz_self_process_id(out);}
static uint32_t query(void*owner,uint64_t at,IzPs2MemoryRegion*out){(void)owner;return iz_self_query(out,at);}
static uint32_t allocate(void*owner,uint64_t bytes,uint64_t*out){
 IzPs2SdkMemory*s=owner;*out=0;return ((uint32_t(*)(uint64_t*,uint64_t,uint64_t))(uintptr_t)(s->sdk+IZ_PS2_MEMALIGN_OFFSET))(out,4096,bytes);
}
static uint32_t release(void*owner,uint64_t pointer){
 IzPs2SdkMemory*s=owner;((void(*)(void*))(uintptr_t)(s->sdk+IZ_PS2_FREE_OFFSET))((void*)(uintptr_t)pointer);return 0;
}
uint32_t iz_ps2_sdk_check_cut(uint64_t sdk,uint64_t offset,const uint8_t*expected,uint64_t bytes){
 if(!sdk||sdk%4096||sdk>=(UINT64_C(1)<<39)||IZ_PS2_SDK_BYTES>(UINT64_C(1)<<39)-sdk||!expected||!bytes||offset>IZ_PS2_SDK_BYTES||bytes>IZ_PS2_SDK_BYTES-offset)return 0xed01;
 uint64_t begin=sdk+offset,end=begin+bytes,at=begin;unsigned regions=0;
 while(at<end){
  IzPs2MemoryRegion r={0};uint32_t rc=iz_self_query(&r,at);
  if(rc||++regions>16||!r.base||r.base%4096||!r.bytes||r.bytes%4096||r.base>=(UINT64_C(1)<<39)||r.bytes>(UINT64_C(1)<<39)-r.base||r.base>at||r.base+r.bytes<=at||!r.type||r.permission!=5||r.attributes||r.ipc_refs||r.device_refs)return 0xed02;
  uint64_t next=r.base+r.bytes;at=next<end?next:end;
 }
 const volatile uint8_t*actual=(const void*)(uintptr_t)begin;for(uint64_t i=0;i<bytes;i++)if(actual[i]!=expected[i])return 0xed03;return 0;
}
uint32_t iz_ps2_sdk_memory_bind(IzPs2SdkMemory*out,const IzNativeEnvironment*e){
 if(!out||!e||e->version!=1||e->bytes!=sizeof *e||e->sdk_bytes!=IZ_PS2_SDK_BYTES||!e->sdk||e->sdk%4096||e->sdk>=(UINT64_C(1)<<39)||e->sdk_bytes>(UINT64_C(1)<<39)-e->sdk||!e->application_pid||e->application_pid>=(UINT64_C(1)<<32))return 0xed01;
 uint64_t pid=0;uint32_t rc=iz_self_process_id(&pid);if(rc||pid!=e->application_pid)return 0xed04;
 rc=iz_ps2_sdk_check_cut(e->sdk,IZ_PS2_MEMALIGN_OFFSET,iz_ps2_memalign_cut,sizeof iz_ps2_memalign_cut);if(rc)return rc;
 rc=iz_ps2_sdk_check_cut(e->sdk,IZ_PS2_FREE_OFFSET,iz_ps2_free_cut,sizeof iz_ps2_free_cut);if(rc)return rc;
 rc=iz_self_process_id(&pid);if(rc||pid!=e->application_pid)return 0xed04;
 out->sdk=e->sdk;out->ops.owner=out;out->ops.identity=identity;out->ops.allocate=allocate;out->ops.query=query;out->ops.release=release;return 0;
}
