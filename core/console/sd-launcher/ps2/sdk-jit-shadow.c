#include "sdk-jit-shadow.h"
#include "gpu-profile.h"
extern uint32_t iz_self_process_id(uint64_t*);
extern uint32_t iz_self_query(IzPs2MemoryRegion*,uint64_t);
extern void*memset(void*,int,size_t);
extern void*memcpy(void*,const void*,size_t);
#define LIMIT (UINT64_C(1)<<39)
static uint64_t address(IzPs2SdkShadow*s,unsigned index){return s->vulkan+iz_ps2_gpu_profile[index].offset;}
static uint32_t identity(void*owner,uint64_t*out){(void)owner;return iz_self_process_id(out);}
static uint32_t query(void*owner,uint64_t at,IzPs2MemoryRegion*out){(void)owner;return iz_self_query(out,at);}
static uint32_t physical(void*owner,uint64_t instance,uint32_t*count,uint64_t*values){return ((uint32_t(*)(uint64_t,uint32_t*,uint64_t*))(uintptr_t)address(owner,IZ_PS2_GPU_PHYSICAL))(instance,count,values);}
static uint32_t properties(void*owner,uint64_t physical,uint8_t*out){((void(*)(uint64_t,void*))(uintptr_t)address(owner,IZ_PS2_GPU_PROPERTIES))(physical,out);return 0;}
static uint32_t allocate(void*owner,uint64_t device,const uint8_t*info,uint64_t*out){return ((uint32_t(*)(uint64_t,const void*,uint64_t,uint64_t*))(uintptr_t)address(owner,IZ_PS2_GPU_ALLOCATE))(device,info,0,out);}
static uint32_t map(void*owner,uint64_t device,uint64_t memory,uint64_t bytes,uint64_t*out){return ((uint32_t(*)(uint64_t,uint64_t,uint64_t,uint64_t,uint32_t,uint64_t*))(uintptr_t)address(owner,IZ_PS2_GPU_MAP))(device,memory,0,bytes,0,out);}
static uint32_t unmap(void*owner,uint64_t device,uint64_t memory){((void(*)(uint64_t,uint64_t))(uintptr_t)address(owner,IZ_PS2_GPU_UNMAP))(device,memory);return 0;}
static uint32_t release(void*owner,uint64_t device,uint64_t memory){((void(*)(uint64_t,uint64_t,uint64_t))(uintptr_t)address(owner,IZ_PS2_GPU_FREE))(device,memory,0);return 0;}
static uint32_t check(uint64_t base,unsigned index){
 uint64_t begin=base+iz_ps2_gpu_profile[index].offset,end=begin+32,at=begin;unsigned regions=0;
 while(at<end){IzPs2MemoryRegion r={0};uint32_t rc=iz_self_query(&r,at);if(rc||++regions>2||!r.base||r.base%4096||!r.bytes||r.bytes%4096||r.base>=LIMIT||r.bytes>LIMIT-r.base||r.base>at||r.base+r.bytes<=at||!r.type||r.permission!=5||r.attributes||r.ipc_refs||r.device_refs)return 0xf222;uint64_t next=r.base+r.bytes;at=next<end?next:end;}
 const volatile uint8_t*p=(void*)(uintptr_t)begin;for(unsigned k=0;k<32;k++)if(p[k]!=iz_ps2_gpu_profile[index].cut[k])return 0xf223;return 0;
}
uint32_t iz_ps2_sdk_shadow_bind(IzPs2SdkShadow*out,const IzNativeEnvironment*e,const IzNativeGraphics*g){
 if(!e||!e->sdk||e->sdk%4096||e->sdk>=LIMIT||e->sdk_bytes>LIMIT-e->sdk)return 0xf221;
 if(!out||!e||!g||e->version!=1||e->bytes!=sizeof *e||e->sdk_bytes!=IZ_PS2_SDK_BYTES||!e->application_pid||e->application_pid>=(UINT64_C(1)<<32)||g->version!=1||g->bytes!=sizeof *g||g->function_count!=IZ_GFX_COUNT||!g->vulkan||g->vulkan%4096||g->vulkan>=LIMIT||IZ_PS2_VULKAN_BYTES>LIMIT-g->vulkan||!g->instance||g->instance>=LIMIT||!g->physical||g->physical>=LIMIT||!g->device||g->device>=LIMIT||g->functions[IZ_GFX_ALLOCATE_MEMORY]!=g->vulkan+iz_ps2_gpu_profile[IZ_PS2_GPU_ALLOCATE].offset||g->functions[IZ_GFX_FREE_MEMORY]!=g->vulkan+iz_ps2_gpu_profile[IZ_PS2_GPU_FREE].offset)return 0xf221;
 uint64_t pid=0;uint32_t rc=iz_self_process_id(&pid);if(rc||pid!=e->application_pid)return 0xf224;for(unsigned i=0;i<IZ_PS2_GPU_FUNCTIONS;i++){rc=check(g->vulkan,i);if(rc)return rc;}rc=iz_self_process_id(&pid);if(rc||pid!=e->application_pid)return 0xf224;
 out->vulkan=g->vulkan;out->ops.owner=out;out->ops.identity=identity;out->ops.query=query;out->ops.physical=physical;out->ops.properties=properties;out->ops.allocate=allocate;out->ops.map=map;out->ops.unmap=unmap;out->ops.release=release;return 0;
}
uint32_t iz_ps2_gpu_dispatch(uint64_t base,uint8_t out[4096]){if(!out||!base||base%4096||base>=LIMIT||IZ_PS2_VULKAN_BYTES>LIMIT-base)return 0xf221;memset(out,0,4096);for(unsigned i=0;i<IZ_PS2_GPU_FUNCTIONS;i++){uint64_t fn=base+iz_ps2_gpu_profile[i].offset;memcpy(out+iz_ps2_gpu_profile[i].dispatch,&fn,8);}return 0;}
