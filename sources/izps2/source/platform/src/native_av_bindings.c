#include "native_av_bindings.h"
#include <string.h>
#include "native_av_cuts.inc" /* generated solely from pinned local RX captures */
_Static_assert(sizeof(uintptr_t)==8,"AArch64 ABI only");
_Static_assert(sizeof(Ps2LauncherVideoLease)==224,"PS2 Launcher lease wire ABI");
_Static_assert(offsetof(Ps2LauncherVideoLease,mapped)==192,"mapped ABI");
_Static_assert(offsetof(Ps2LauncherVideoLease,image_count)==212,"image count ABI");
static int outside(uintptr_t a,size_t n,uintptr_t b,size_t m){
    return n<=UINTPTR_MAX-a&&m<=UINTPTR_MAX-b&&(a+n<=b||b+m<=a);
}
static int verify(uintptr_t base,size_t size,const AvCut*cuts,unsigned count){
    if(!base||(base&3)||size>UINTPTR_MAX-base)return PS2_AV_INVALID;
    for(unsigned i=0;i<count;i++){
        const AvCut*c=&cuts[i];if(c->offset>size||c->bytes>size-c->offset)return PS2_AV_INVALID;
        if(memcmp((const void*)(base+c->offset),c->expected,c->bytes))return PS2_AV_BYTES;
    }
    return 0;
}
int ps2_sdk_audio_bind(Ps2SdkAudioBinding*b,uintptr_t rx,size_t bytes,void*arena,size_t n,unsigned tls){
    if(!b||b->verified||b->live||b->retained||tls!=1||!arena||((uintptr_t)arena&4095)||n<PS2_AUDIO_ARENA_BYTES||
       !outside((uintptr_t)b,sizeof(*b),(uintptr_t)arena,n)||!outside((uintptr_t)b,sizeof(*b),rx,bytes)||
       !outside((uintptr_t)arena,n,rx,bytes))return PS2_AV_INVALID;
    const int result=verify(rx,bytes,sdk_cuts,sizeof sdk_cuts/sizeof* sdk_cuts);if(result)return result;
    Ps2SdkAudioBinding value={0};
    for(unsigned i=0;i<PS2_AUDIO_FUNCTIONS;i++)value.audio.functions[i]=rx+sdk_cuts[i].offset;
    value.audio.arena=arena;value.audio.arena_bytes=n;value.audio.startup_slots=1;
    for(unsigned i=0;i<6;i++)value.worker[i]=rx+sdk_cuts[PS2_AUDIO_FUNCTIONS+i].offset;
    value.audio.sleep_ns=(void(*)(uint64_t))(rx+sdk_cuts[PS2_AUDIO_FUNCTIONS+6].offset);
    value.verified=1;*b=value;return 0;
}
int ps2_sdk_audio_start_worker(void*context,void*(*entry)(void*),void*argument,uint64_t*out){
    Ps2SdkAudioBinding*b=context;
    if(!b||!b->verified||b->live||b->retained||!entry||!out||((uintptr_t)out&7)||
       !outside((uintptr_t)out,sizeof(*out),(uintptr_t)b,sizeof(*b))||
       !outside((uintptr_t)out,sizeof(*out),(uintptr_t)b->audio.arena,b->audio.arena_bytes))return 2;
    memset(b->attributes,0,sizeof b->attributes);
    int r=((int(*)(void*))b->worker[0])(b->attributes);
    if(r){b->last_sdk_error=(uint32_t)r;b->retained=1;return 2;}
    r=((int(*)(void*,size_t))b->worker[2])(b->attributes,2*1024*1024);
    if(!r)r=((int(*)(void*,int))b->worker[3])(b->attributes,0);
    if(r){int clean=((int(*)(void*))b->worker[1])(b->attributes);b->last_sdk_error=(uint32_t)r;
        if(clean){b->retained=1;return 2;}return 1;}
    uint64_t id=0;r=((int(*)(uint64_t*,const void*,void*(*)(void*),void*))b->worker[4])(&id,b->attributes,entry,argument);
    if(r||!id){b->thread=id;b->live=!!id;b->retained=1;b->last_sdk_error=(uint32_t)r;return 2;}
    b->thread=id;b->live=1;b->created++;*out=id;
    r=((int(*)(void*))b->worker[1])(b->attributes);
    if(r){b->retained=1;b->last_sdk_error=(uint32_t)r;return 2;}return 0;
}
int ps2_sdk_audio_join_worker(void*context,uint64_t thread){
    Ps2SdkAudioBinding*b=context;
    if(!b||!b->verified||!b->live||b->retained||thread!=b->thread)return 2;
    void*returned=0;int r=((int(*)(uint64_t,void**))b->worker[5])(thread,&returned);
    if(r){b->retained=1;b->last_sdk_error=(uint32_t)r;return 2;}
    b->live=0;b->thread=0;b->joined++;return 0;
}
int ps2_sdk_video_bind(Ps2SdkVideoBinding*b,uintptr_t rx,size_t bytes,const Ps2LauncherVideoLease*l){
    if(!b||!l||b->bound||b->retained||l->magic!=PS2_VIDEO_LEASE_MAGIC||l->version!=1||l->bytes!=sizeof(*l)||
       l->flags!=PS2_VIDEO_LEASE_FLAGS||(!l->token[0]&&!l->token[1])||!l->device||!l->queue||!l->swapchain||!l->acquire_semaphore||
       !l->mapped||(l->mapped&3)||l->mapped_bytes<4096+1024*1024*4||(l->memory_flags&6)!=6||
       !l->image_count||l->image_count>8||l->max_width!=1024||l->max_height!=1024||
       !outside((uintptr_t)b,sizeof(*b),(uintptr_t)l,sizeof(*l))||
       !outside((uintptr_t)b,sizeof(*b),(uintptr_t)l->mapped,l->mapped_bytes)||
       !outside((uintptr_t)l,sizeof(*l),(uintptr_t)l->mapped,l->mapped_bytes)||
       !outside((uintptr_t)b,sizeof(*b),rx,bytes)||!outside((uintptr_t)l->mapped,l->mapped_bytes,rx,bytes))return PS2_AV_INVALID;
    for(unsigned i=0;i<l->image_count;i++){
        if(!l->commands[i]||!l->semaphores[i]||l->semaphores[i]==l->acquire_semaphore)return PS2_AV_INVALID;
        for(unsigned j=0;j<i;j++)if(l->commands[i]==l->commands[j]||l->semaphores[i]==l->semaphores[j])return PS2_AV_INVALID;
    }
    int r=verify(rx,bytes,vulkan_cuts,sizeof vulkan_cuts/sizeof*vulkan_cuts);if(r)return r;
    Ps2SdkVideoBinding value={0};Ps2NativeVideo*v=&value.video;
    v->device=l->device;v->queue=l->queue;v->swapchain=l->swapchain;v->acquire_semaphore=l->acquire_semaphore;
    v->acquire=rx+vulkan_cuts[0].offset;v->submit=rx+vulkan_cuts[1].offset;
    v->present=rx+vulkan_cuts[2].offset;v->wait_idle=rx+vulkan_cuts[3].offset;
    memcpy(v->commands,l->commands,sizeof v->commands);memcpy(v->semaphores,l->semaphores,sizeof v->semaphores);
    v->mapped=(void*)(uintptr_t)l->mapped;v->mapped_bytes=l->mapped_bytes;v->image_count=l->image_count;
    v->max_width=1024;v->max_height=1024;value.token[0]=l->token[0];value.token[1]=l->token[1];value.bound=1;
    *b=value;return 0;
}
int ps2_sdk_video_finish(Ps2SdkVideoBinding*b){
    if(!b||!b->bound||b->retained)return PS2_AV_RETAINED;
    if(b->video.poisoned){b->retained=1;return PS2_AV_RETAINED;}
    int32_t r=((int32_t(*)(uint64_t))b->video.wait_idle)(b->video.queue);
    if(r){b->video.gpu_error=r;b->video.poisoned=1;b->retained=1;return PS2_AV_RETAINED;}
    b->known_idle=1;return 0;
}
