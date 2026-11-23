#include "native_video.h"
#include <string.h>
#ifdef IZPS2_SB4_RGBA_COPY
#include <sb4/memory.h>
#endif
typedef struct {uint32_t type,pad;uint64_t next,swapchain,timeout,semaphore,fence;uint32_t mask,pad2;} Acquire;
typedef struct {uint32_t type,pad;uint64_t next;uint32_t waits,pad2;uint64_t wait,stage;uint32_t commands,pad3;uint64_t command;uint32_t signals,pad4;uint64_t signal;} Submit;
typedef struct {uint32_t type,pad;uint64_t next;uint32_t waits,pad2;uint64_t wait;uint32_t count,pad3;uint64_t swapchains,indices,results;} Present;
_Static_assert(sizeof(Acquire)==56,"SDK Vulkan acquire ABI");
_Static_assert(sizeof(Submit)==72,"SDK Vulkan submit ABI");
_Static_assert(sizeof(Present)==64,"SDK Vulkan present ABI");
static int overlap(uintptr_t a,size_t n,uintptr_t b,size_t m){
    if(n>UINTPTR_MAX-a || m>UINTPTR_MAX-b)return 1;
    return a<b+m && b<a+n;
}
static int fail(Ps2NativeVideo*v,int stage,int32_t error){v->poisoned=1;v->failure_stage=stage;v->gpu_error=error;return stage;}
static int queue_valid(const Ps2NativeVideo *v, const uint64_t *commands) {
    if (!v || (uintptr_t)v % _Alignof(Ps2NativeVideo) || v->poisoned ||
       !commands || (uintptr_t)commands % _Alignof(uint64_t) ||
       !v->device||!v->queue||!v->swapchain||!v->acquire_semaphore||
       (!v->execute_queue&&(!v->acquire||!v->submit||!v->present||!v->wait_idle))||
       !v->image_count||v->image_count>8) return 0;
    for(unsigned i=0;i<v->image_count;i++)
        if(!commands[i]||!v->semaphores[i]) return 0;
    return 1;
}
static int execute_commands(Ps2NativeVideo *v, const uint64_t *commands) {
    if(v->execute_queue)return v->execute_queue(v,commands,v->queue_context);
    uint32_t index=0,stage=0x400;
    Acquire a={1000060010,0,0,v->swapchain,100000000,v->acquire_semaphore,0,1,0};
    int32_t r=((int32_t(*)(uint64_t,const void*,uint32_t*))v->acquire)(v->device,&a,&index);
    if(r||index>=v->image_count)return fail(v,PS2_VIDEO_ACQUIRE,r?r:-1);
    uint64_t signal=v->semaphores[index],command=commands[index];
    Submit s={4,0,0,1,0,(uintptr_t)&v->acquire_semaphore,(uintptr_t)&stage,1,0,(uintptr_t)&command,1,0,(uintptr_t)&signal};
    r=((int32_t(*)(uint64_t,uint32_t,const void*,uint64_t))v->submit)(v->queue,1,&s,0);
    if(r)return fail(v,PS2_VIDEO_SUBMIT,r);
    Present p={1000001001,0,0,1,0,(uintptr_t)&signal,1,0,(uintptr_t)&v->swapchain,(uintptr_t)&index,0};
    r=((int32_t(*)(uint64_t,const void*))v->present)(v->queue,&p);
    int32_t idle=((int32_t(*)(uint64_t))v->wait_idle)(v->queue);
    if(idle)return fail(v,PS2_VIDEO_IDLE,idle);
    if(r)return fail(v,PS2_VIDEO_PRESENT,r);
    v->submissions++;return PS2_VIDEO_OK;
}
int ps2_native_video_execute_commands(Ps2NativeVideo *v, const uint64_t *commands) {
    if (!queue_valid(v, commands)) return PS2_VIDEO_INVALID;
    __atomic_thread_fence(__ATOMIC_RELEASE);
    return execute_commands(v, commands);
}
int ps2_native_video_present(Ps2NativeVideo*v,const void*rgba,size_t bytes,unsigned w,unsigned h,size_t pitch,uint32_t frame){
    if(!v || (uintptr_t)v % _Alignof(Ps2NativeVideo)) return PS2_VIDEO_INVALID;
    if(!queue_valid(v,v->commands)||!rgba||!v->mapped||((uintptr_t)v->mapped&3)||
       !v->max_width||!v->max_height||
       v->max_width>PS2_VIDEO_MAX_WIDTH||v->max_height>PS2_VIDEO_MAX_HEIGHT||
       !w||!h||w>v->max_width||h>v->max_height||pitch<(size_t)w*4||
       pitch>SIZE_MAX/h||bytes<(size_t)(h-1)*pitch+(size_t)w*4||
       v->mapped_bytes<PS2_VIDEO_HEADER_BYTES+(size_t)w*h*4||
       overlap((uintptr_t)v,sizeof(*v),(uintptr_t)v->mapped,v->mapped_bytes)||
       overlap((uintptr_t)rgba,bytes,(uintptr_t)v->mapped,v->mapped_bytes))return PS2_VIDEO_INVALID;
    uint8_t*dst=(uint8_t*)v->mapped+PS2_VIDEO_HEADER_BYTES;
    for(unsigned y=0;y<h;y++){
#ifdef IZPS2_SB4_RGBA_COPY
        sb4CopyOpaqueRgba(dst+(size_t)y*w*4,(const uint8_t*)rgba+(size_t)y*pitch,w);
#else
        memcpy(dst+(size_t)y*w*4,(const uint8_t*)rgba+(size_t)y*pitch,(size_t)w*4);
        for(unsigned x=0;x<w;x++)dst[((size_t)y*w+x)*4+3]=255;
#endif
    }
    uint32_t*header=v->mapped;header[0]=w;header[1]=h;header[2]=frame;
    __atomic_thread_fence(__ATOMIC_RELEASE);
    int result = execute_commands(v, v->commands);
    if (!result) v->copied_bytes+=(uint64_t)w*h*4;
    return result;
}
