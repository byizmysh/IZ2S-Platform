#include "sdk-video.h"
#include "gpu-profile.h"
extern uint32_t iz_self_process_id(uint64_t*);
extern uint32_t iz_self_query(IzPs2MemoryRegion*,uint64_t);
extern void*memcpy(void*,const void*,size_t);
#define LIMIT (UINT64_C(1)<<39)
static uint32_t identity(void*owner,uint64_t*out){(void)owner;return iz_self_process_id(out);}
static uint32_t query(void*owner,uint64_t at,IzPs2MemoryRegion*out){(void)owner;return iz_self_query(out,at);}
static int result_function(uint32_t f){
 switch(f){case IZ_GFX_CREATE_SHADER:case IZ_GFX_CREATE_GRAPHICS:case IZ_GFX_CREATE_COMPUTE:case IZ_GFX_CREATE_BUFFER:case IZ_GFX_ALLOCATE_MEMORY:case IZ_GFX_BIND_BUFFER:case IZ_GFX_MAP_MEMORY:case IZ_GFX_CREATE_SET_LAYOUT:case IZ_GFX_CREATE_LAYOUT:case IZ_GFX_CREATE_POOL:case IZ_GFX_ALLOCATE_SET:case IZ_GFX_ALLOCATE_COMMANDS:case IZ_GFX_BEGIN_COMMAND:case IZ_GFX_END_COMMAND:case IZ_GFX_ACQUIRE:case IZ_GFX_SUBMIT:case IZ_GFX_PRESENT:case IZ_GFX_QUEUE_IDLE:case IZ_PS2_VIDEO_CREATE_SEMAPHORE:return 1;default:return 0;}
}
static uint32_t call(void*owner,uint32_t f,const uint64_t args[8],uint64_t*out){
 IzPs2SdkVideo*s=owner;if(!s||!s->bound||!args||!out||f>=IZ_PS2_VIDEO_FUNCTIONS||!s->functions[f])return 0xf321;
 uint64_t(*fn)(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t)=(void*)(uintptr_t)s->functions[f];uint64_t value=fn(args[0],args[1],args[2],args[3],args[4],args[5],args[6],args[7]);*out=result_function(f)?value:0;return 0;
}
static uint32_t executable(uint64_t base,uint64_t pointer){
 if(!pointer||pointer%4||pointer<base||pointer-base>IZ_PS2_VULKAN_BYTES-32)return 0xf322;
 uint64_t at=pointer,end=pointer+32;unsigned count=0;while(at<end){IzPs2MemoryRegion r={0};if(iz_self_query(&r,at)||++count>2||!r.base||r.base%4096||!r.bytes||r.bytes%4096||r.base>=LIMIT||r.bytes>LIMIT-r.base||r.base>at||r.base+r.bytes<=at||!r.type||r.permission!=5||r.attributes||r.ipc_refs||r.device_refs)return 0xf322;uint64_t next=r.base+r.bytes;at=next<end?next:end;}return 0;
}
uint32_t iz_ps2_sdk_video_bind(IzPs2SdkVideo*out,const IzNativeEnvironment*e,const IzNativeGraphics*g){
 if(!out||!e||!g||out->bound||out->reserved||e->version!=1||e->bytes!=sizeof *e||e->sdk_bytes!=IZ_PS2_SDK_BYTES||!e->application_pid||e->application_pid>=(UINT64_C(1)<<32)||g->version!=1||g->bytes!=sizeof *g||g->function_count!=IZ_GFX_COUNT||!g->vulkan||g->vulkan%4096||g->vulkan>=LIMIT||IZ_PS2_VULKAN_BYTES>LIMIT-g->vulkan||!g->instance||!g->physical||!g->device||!g->queue||!g->command_pool||!g->swapchain)return 0xf321;
 uint64_t a=(uintptr_t)out,b=(uintptr_t)e,c=(uintptr_t)g;
 if(a>UINT64_MAX-sizeof *out||b>UINT64_MAX-sizeof *e||c>UINT64_MAX-sizeof *g||(a<b+sizeof *e&&b<a+sizeof *out)||(a<c+sizeof *g&&c<a+sizeof *out))return 0xf321;
 const uint8_t*prior=(const void*)out;for(size_t i=0;i<sizeof *out;i++)if(prior[i])return 0xf321;
 uint64_t pid=0;if(iz_self_process_id(&pid)||pid!=e->application_pid)return 0xf323;
 uint64_t resolver=g->vulkan+iz_ps2_gpu_profile[IZ_PS2_GPU_PROC].offset;uint32_t rc=executable(g->vulkan,resolver);if(rc)return rc;const volatile uint8_t*actual=(void*)(uintptr_t)resolver;for(unsigned i=0;i<32;i++)if(actual[i]!=iz_ps2_gpu_profile[IZ_PS2_GPU_PROC].cut[i])return 0xf324;
 uint64_t functions[IZ_PS2_VIDEO_FUNCTIONS];for(unsigned i=0;i<IZ_GFX_COUNT;i++){functions[i]=g->functions[i];rc=executable(g->vulkan,functions[i]);if(rc)return rc;}
 /* Resolve exact API names through the byte-qualified driver entry, then
  * admit their returned RX spans. No session-specific offset is invented. */
 uint64_t(*proc)(uint64_t,const char*)=(void*)(uintptr_t)resolver;
 functions[IZ_PS2_VIDEO_CREATE_SEMAPHORE]=proc(g->instance,"vkCreateSemaphore");functions[IZ_PS2_VIDEO_DESTROY_SEMAPHORE]=proc(g->instance,"vkDestroySemaphore");
 for(unsigned i=IZ_GFX_COUNT;i<IZ_PS2_VIDEO_FUNCTIONS;i++){rc=executable(g->vulkan,functions[i]);if(rc)return rc;}
 if(iz_self_process_id(&pid)||pid!=e->application_pid)return 0xf323;
 memcpy(out->functions,functions,sizeof functions);out->ops=(IzPs2VideoOps){out,identity,query,call};out->bound=1;return 0;
}
