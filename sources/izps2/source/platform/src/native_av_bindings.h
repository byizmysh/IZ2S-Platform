#ifndef PS2_NATIVE_AV_BINDINGS_H
#define PS2_NATIVE_AV_BINDINGS_H
#include "native_audio.h"
#include "native_video.h"
#ifdef __cplusplus
extern "C" {
#endif
#define PS2_VIDEO_LEASE_MAGIC 0x32564c50u
#define PS2_VIDEO_LEASE_FLAGS 7u /* exclusive queue + PS2 commands + HOST barrier */
typedef struct Ps2LauncherVideoLease {
    uint32_t magic,version,bytes,flags;
    uint64_t token[2];
    uint64_t device,queue,swapchain,acquire_semaphore;
    uint64_t commands[8],semaphores[8];
    uint64_t mapped,mapped_bytes;
    uint32_t memory_flags,image_count,max_width,max_height;
} Ps2LauncherVideoLease;
typedef struct Ps2SdkVideoBinding {
    Ps2NativeVideo video;
    uint64_t token[2];
    uint32_t bound,known_idle,retained;
} Ps2SdkVideoBinding;
typedef struct Ps2SdkAudioBinding {
    Ps2NativeAudioConfig audio;
    uintptr_t worker[6]; /* attr init/destroy/stack/detach, create, join */
    uint64_t thread,created,joined;
    uint32_t verified,live,retained,last_sdk_error;
#ifdef __cplusplus
    alignas(16) unsigned char attributes[64]; /* SDK writes exactly 56 */
#else
    _Alignas(16) unsigned char attributes[64];
#endif
} Ps2SdkAudioBinding;
enum Ps2AvResult {PS2_AV_OK=0,PS2_AV_INVALID=200,PS2_AV_BYTES=201,PS2_AV_RETAINED=202};
/* rx must be a caller-verified readable executable module mapping, resident
 * until every user has joined. These routines read actual bytes before binding;
 * a recorded capture is used only by the offline validator, never as execution.
 * Failure changes no output and performs no SDK call. Rebinding a live owner
 * is forbidden. No caller-provided arbitrary function address is accepted.
 */
int ps2_sdk_audio_bind(Ps2SdkAudioBinding*,uintptr_t sdk_rx,size_t sdk_rx_bytes,
    void*arena,size_t arena_bytes,unsigned tls_runtime_ready);
int ps2_sdk_video_bind(Ps2SdkVideoBinding*,uintptr_t vulkan_rx,size_t vulkan_rx_bytes,
    const Ps2LauncherVideoLease*);
/* Call from the one serialized AudioStream control owner, after TLS is bound.
 * Start0 means created;1 definitely not created;2 unknown/retained. Join0
 * means joined. No detached worker, Linux pthread object or std::thread used.
 */
int ps2_sdk_audio_start_worker(void*,void*(*)(void*),void*,uint64_t*);
int ps2_sdk_audio_join_worker(void*,uint64_t);
int ps2_sdk_audio_install_stream(Ps2SdkAudioBinding*);
/* Only after VM/MTGS are joined: prove final QueueWaitIdle. Does not destroy
 * or free any borrowed Launcher object. Poison/error retains the whole lease.
 */
int ps2_sdk_video_finish(Ps2SdkVideoBinding*);
#ifdef __cplusplus
}
#endif
#endif
