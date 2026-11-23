#include "hbxl_vm_av.h"
#include "hbxl_vm_frontend.h"
#include "hbxl_code_domains.h"
#include "native_audio_stream.h"
#include "sw_scanout.h"
#include "hbxl.h"
#include "hbx_sdk.h"
#include "Host/AudioStream.h"
#include "SPU2/spu2.h"
#include <atomic>
#include <cerrno>
#include <pthread.h>

static_assert(sizeof(Ps2HbxlVmAvConfig)==288);
static_assert(offsetof(Ps2HbxlVmAvConfig,video)==64);
namespace {
constexpr uint64_t limit=1ull<<39,sdk_bytes=6164480,vulkan_bytes=45989888;
constexpr int invalid=300,worker_unknown=301,audio_failed=302,join_missing=303;
struct Owner {
    Ps2SdkAudioBinding audio{};
    Ps2SdkVideoBinding video{};
    Ps2SwScanout scanout{};
    pthread_t thread{};
    void*(*entry)(void*)=nullptr;void*argument=nullptr;
    int*control_errno=nullptr;
    std::atomic<uint64_t> frames{0},appends{0},opens{0},closes{0},workers{0},joins{0};
    std::atomic<unsigned> error{0},tls_checked{0},retained{0};
    unsigned initialized=0,live_worker=0,closed_joined=0,gs_joined=0,shutdown=0;
} owner;
bool range(uint64_t p,uint64_t n,unsigned alignment){return p&&!(p&(alignment-1))&&n&&p<limit&&n<=limit-p;}
bool separate(uint64_t a,uint64_t n,uint64_t b,uint64_t m){return a+n<=b||b+m<=a;}
void failure(unsigned e){unsigned zero=0;owner.error.compare_exchange_strong(zero,e,std::memory_order_release);}
void retained(void*,int e){owner.retained.store(1,std::memory_order_release);failure(e?e:audio_failed);}
void*worker_entry(void*){
    // libc errno resolves through the PRIVATE musl TP set by HBXL clone. A raw
    // SDK pthread is forbidden here, even when emulated C++ TLS was installed.
    if(&errno==owner.control_errno){retained(nullptr,worker_unknown);return nullptr;}
    owner.tls_checked.store(1,std::memory_order_release);
    return owner.entry(owner.argument);
}
int start_worker(void*,void*(*entry)(void*),void*argument,uint64_t*out){
    if(!entry||!out||owner.live_worker||owner.retained.load())return 2;
    *out=0;
    pthread_attr_t attr{};int r=pthread_attr_init(&attr);
    if(r)return 1; // No create call has occurred.
    r=pthread_attr_setstacksize(&attr,2*1024*1024);
    if(!r)r=pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_JOINABLE);
    if(r){if(pthread_attr_destroy(&attr)){retained(nullptr,worker_unknown);return 2;}return 1;}
    owner.entry=entry;owner.argument=argument;owner.control_errno=&errno;owner.thread=0;
    r=pthread_create(&owner.thread,&attr,worker_entry,nullptr);
    if(r){
        // Private musl pthread_create returns an error without publishing a
        // child. HBXL clone rejects allocation/CreateThread before StartThread.
        const int destroyed=pthread_attr_destroy(&attr);
        owner.thread=0;owner.entry=nullptr;owner.argument=nullptr;owner.control_errno=nullptr;
        if(destroyed){retained(nullptr,worker_unknown);return 2;}
        return 1;
    }
    if(!owner.thread){(void)pthread_attr_destroy(&attr);retained(nullptr,worker_unknown);return 2;}
    owner.live_worker=1;owner.workers.fetch_add(1,std::memory_order_release);
    *out=(uintptr_t)owner.thread;
    if(pthread_attr_destroy(&attr)){retained(nullptr,worker_unknown);return 2;}
    return 0;
}
int join_worker(void*,uint64_t thread){
    if(!owner.live_worker||thread!=(uintptr_t)owner.thread||owner.retained.load())return 2;
    if(pthread_join(owner.thread,nullptr)){retained(nullptr,worker_unknown);return 2;}
    owner.live_worker=0;owner.joins.fetch_add(1,std::memory_order_release);
    // Initial NativeAudioStream can close and join before SPU2::Open returns
    // nullptr. Close is published only after native drain proves no borrowed
    // buffers; this successful join completes that independent receipt.
    if(!owner.error.load(std::memory_order_acquire)&&!owner.retained.load(std::memory_order_acquire)&&
       owner.opens.load(std::memory_order_acquire)&&owner.opens.load(std::memory_order_acquire)==owner.closes.load(std::memory_order_acquire)&&
       owner.workers.load(std::memory_order_acquire)==owner.joins.load(std::memory_order_acquire))owner.closed_joined=1;
    return 0;
}
uint32_t audio_open(void*object,void*parameters){
    const uint32_t r=((uint32_t(*)(void*,void*))owner.audio.audio.functions[PS2_AUDIO_OPEN])(object,parameters);
    if(!r)owner.opens.fetch_add(1,std::memory_order_release);else failure(audio_failed);
    return r;
}
uint32_t audio_append(void*object,void*descriptor){
    const uint32_t r=((uint32_t(*)(void*,void*))owner.audio.audio.functions[PS2_AUDIO_APPEND])(object,descriptor);
    if(r==1)owner.appends.fetch_add(1,std::memory_order_release);
    else if(r)failure(PS2_AUDIO_OWNERSHIP);
    return r;
}
void audio_close(void*object){
    ((void(*)(void*))owner.audio.audio.functions[PS2_AUDIO_CLOSE])(object);
    owner.closes.fetch_add(1,std::memory_order_release);
}
}
extern "C" int ps2_hbxl_vm_av_validate_configuration(const Ps2HbxlVmAvConfig*c){
    if(!c||c->magic!=PS2_HBXL_VM_AV_MAGIC||c->version!=1||c->size!=sizeof(*c)||c->flags!=1||
       !range(c->vulkan_rx,c->vulkan_rx_bytes,4096)||c->vulkan_rx_bytes!=vulkan_bytes||
       !range(c->audio_arena,c->audio_arena_bytes,4096)||c->audio_arena_bytes!=PS2_AUDIO_ARENA_BYTES||
       !range(c->scratch,c->scratch_bytes,32)||c->scratch_bytes<PS2_HBXL_VM_AV_SCRATCH_BYTES||
       !range(c->video.mapped,c->video.mapped_bytes,4)||c->video.mapped_bytes<4096+1024*1024*4)return invalid;
    const uint64_t p[]={c->vulkan_rx,c->audio_arena,c->scratch,c->video.mapped,(uintptr_t)c,(uintptr_t)&owner};
    const uint64_t n[]={c->vulkan_rx_bytes,c->audio_arena_bytes,c->scratch_bytes,c->video.mapped_bytes,sizeof(*c),sizeof(owner)};
    for(unsigned i=0;i<6;i++)for(unsigned j=0;j<i;j++)if(!separate(p[i],n[i],p[j],n[j]))return invalid;
    return 0;
}
extern "C" int ps2_hbxl_vm_av_initialize(){
    if(owner.initialized||owner.retained.load()||owner.shutdown||!hbxl_ctx||hbxl_ctx->magic!=HBX_ABI_MAGIC||
       hbxl_ctx->abi_version!=3||hbxl_ctx->size<sizeof(*hbxl_ctx)||hbxl_ctx->flags||
       !range(hbxl_ctx->args[2],sizeof(Ps2HbxlVmAvConfig),8)||hbxl_ctx->audio.initialize_parameter<0x1c7f30)return invalid;
    const auto*c=(const Ps2HbxlVmAvConfig*)(uintptr_t)hbxl_ctx->args[2];
    int r=ps2_hbxl_vm_av_validate_configuration(c);if(r)return r;
    const uint64_t sdk=hbxl_ctx->audio.initialize_parameter-0x1c7f30;
    if(!range(sdk,sdk_bytes,4096)||!separate(sdk,sdk_bytes,c->audio_arena,c->audio_arena_bytes)||
       !separate(sdk,sdk_bytes,c->scratch,c->scratch_bytes)||!separate(sdk,sdk_bytes,c->video.mapped,c->video.mapped_bytes)||
       !separate(sdk,sdk_bytes,c->vulkan_rx,c->vulkan_rx_bytes)||!separate(sdk,sdk_bytes,(uintptr_t)c,sizeof(*c)))return invalid;
    // The final image links private HBXL libc/thread startup. The generic bind
    // admits AudioOut bytes only; its raw SDK pthread entrypoints stay unused.
    r=ps2_sdk_video_bind(&owner.video,c->vulkan_rx,c->vulkan_rx_bytes,&c->video);if(r)return r;
    r=ps2_sdk_audio_bind(&owner.audio,sdk,sdk_bytes,(void*)(uintptr_t)c->audio_arena,c->audio_arena_bytes,1);if(r)return r;
    Ps2LiveAudioHost host{};host.audio=owner.audio.audio;
    host.audio.functions[PS2_AUDIO_OPEN]=(uintptr_t)audio_open;
    host.audio.functions[PS2_AUDIO_APPEND]=(uintptr_t)audio_append;
    host.audio.functions[PS2_AUDIO_CLOSE]=(uintptr_t)audio_close;
    host.context=&owner;host.start_worker=start_worker;host.join_worker=join_worker;
    host.sleep_ns=host.audio.sleep_ns;host.retained=retained;
    host.pull_frames=1024;host.control_wait_ms=3000;host.drain_wait_ms=3000;
    r=ps2_live_audio_install(&host);if(r)return r;
    owner.scanout.video=&owner.video.video;owner.scanout.scratch=(void*)(uintptr_t)c->scratch;owner.scanout.scratch_bytes=c->scratch_bytes;
    owner.initialized=1;return 0;
}
extern "C" int ps2_hbxl_vm_av_error(){return owner.error.load(std::memory_order_acquire);}
extern "C" int ps2_hbxl_vm_av_refresh(){
    if(!owner.initialized||owner.shutdown||ps2_hbxl_vm_av_error())return invalid;
    const int r=ps2_sw_scanout_repeat(&owner.scanout);
    if(r&&r!=PS2_SCANOUT_NO_DISPLAY)failure(r);
    return r;
}
extern "C" int ps2_hbxl_vm_av_ready(){return owner.initialized&&!owner.shutdown&&!ps2_hbxl_vm_av_error()&&
    owner.frames.load(std::memory_order_acquire)&&owner.appends.load(std::memory_order_acquire)&&owner.tls_checked.load(std::memory_order_acquire);}
extern "C" void __real__Z7GSvsyncjb(unsigned,bool);
extern "C" void __wrap__Z7GSvsyncjb(unsigned field,bool registers_written){
    __real__Z7GSvsyncjb(field,registers_written);
    if(!owner.initialized||owner.shutdown||ps2_hbxl_vm_av_error())return;
    const int r=ps2_sw_scanout_present(&owner.scanout,2);
    if(!r)owner.frames.fetch_add(1,std::memory_order_release);
    else if(r!=PS2_SCANOUT_NO_DISPLAY)failure(r);
}
extern "C" void __real__ZN4SPU25CloseEv();
extern "C" void __wrap__ZN4SPU25CloseEv(){
    if(owner.initialized){
        AudioStream*s=SPU2::GetOutputStream();
        if(s){
            // Original VMManager stops the IOP producer before this callsite.
            const int r=ps2_live_audio_shutdown(s);Ps2LiveAudioStatus status{};
            if(r||!ps2_live_audio_status(s,&status)||!status.closed||!status.worker_joined||status.retained||status.pending||status.uncertain)
                ps2_hbxl_vm_fatal("native AudioOut drain/close/private join unproved",__LINE__);
            owner.closed_joined=1;
        }
    }
    __real__ZN4SPU25CloseEv();
}
extern "C" std::unique_ptr<AudioStream> __wrap__ZN11AudioStream16CreateNullStreamEjj(unsigned,unsigned){
    ps2_hbxl_vm_fatal("native AV profile forbids Null AudioStream fallback",__LINE__);
}
extern "C" int ps2_hbxl_vm_av_shutdown(){
    if(!owner.initialized||owner.shutdown)return invalid;
    Ps2HbxlCodeDomainsStatistics s{};ps2_hbxl_code_domains_statistics(&s);
    if(!s.gs_joined||(!s.gs_owner&&s.gs_publications)||s.last_domain_error||s.code.poisoned||s.code.live_pages||s.code.mapped_pages||s.code.maps!=s.code.unmaps){
        owner.retained.store(1,std::memory_order_release);failure(join_missing);return join_missing;
    }
    owner.gs_joined=1;
    // A boot error can precede SPU2::Open. No open or worker means there is
    // nothing to drain; a partial/uncertain open still requires its receipts.
    const bool never_opened=!owner.opens.load()&&!owner.closes.load()&&!owner.appends.load()&&
        !owner.workers.load()&&!owner.joins.load()&&!owner.entry&&!owner.thread;
    if(ps2_hbxl_vm_av_error()||owner.retained.load()||owner.live_worker||(!never_opened&&!owner.closed_joined)||
       owner.workers.load()!=owner.joins.load()||(!never_opened&&!owner.opens.load())||owner.opens.load()!=owner.closes.load())return audio_failed;
    const int r=ps2_sdk_video_finish(&owner.video);if(r){owner.retained.store(1,std::memory_order_release);failure(r);return r;}
    owner.shutdown=1;return 0;
}
extern "C" void ps2_hbxl_vm_av_statistics(Ps2HbxlVmAvStatistics*s){
    if(!s)return;*s={owner.frames.load(std::memory_order_acquire),owner.appends.load(std::memory_order_acquire),
        owner.opens.load(std::memory_order_acquire),owner.closes.load(std::memory_order_acquire),owner.workers.load(std::memory_order_acquire),owner.joins.load(std::memory_order_acquire),
        owner.error.load(std::memory_order_acquire),owner.initialized,owner.tls_checked.load(std::memory_order_acquire),owner.closed_joined,
        owner.gs_joined,owner.video.known_idle,owner.retained.load(std::memory_order_acquire),owner.shutdown};
}
