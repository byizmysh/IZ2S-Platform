#ifndef PS2_NATIVE_VIDEO_H
#define PS2_NATIVE_VIDEO_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Same 4096-byte header + packed little-endian RGBA SSBO used by the
 * qualified PS1 display. The caller owns one serial CPU producer, coherent
 * mapped memory, pre-recorded graphics commands and the Vulkan queue.
 * A successful frame call waits for completion of the GPU read of the
 * framebuffer before returning. No source pixels are borrowed afterward.
 * A failed GPU operation poisons this owner; its resources must stay resident.
 * This does not allocate or create a device/swapchain. */
#define PS2_VIDEO_HEADER_BYTES 4096u
#define PS2_VIDEO_MAX_WIDTH 1024u
#define PS2_VIDEO_MAX_HEIGHT 1024u
typedef struct Ps2NativeVideo {
    uint64_t device,queue,swapchain,acquire_semaphore;
    uintptr_t acquire,submit,present,wait_idle;
    uint64_t commands[8],semaphores[8];
    void *mapped;
    size_t mapped_bytes;
    uint32_t image_count,max_width,max_height,poisoned;
    uint64_t submissions,copied_bytes;
    int32_t gpu_error;
    uint32_t failure_stage;
    /* Optional typed platform dispatch. The serial queue contract is shared. */
    int (*execute_queue)(struct Ps2NativeVideo *, const uint64_t *, void *);
    void *queue_context;
} Ps2NativeVideo;
enum { PS2_VIDEO_OK=0, PS2_VIDEO_INVALID=1, PS2_VIDEO_ACQUIRE=2,
       PS2_VIDEO_SUBMIT=3, PS2_VIDEO_PRESENT=4, PS2_VIDEO_IDLE=5 };
/* Source is RGBA8 from real GS texture readback. Alpha is forced opaque;
 * the existing display shader uses RGB only. No pixel scaling is performed.
 * Validation errors make no GPU calls and do not modify mapped memory. */
int ps2_native_video_present(Ps2NativeVideo *,const void *rgba,size_t bytes,
                             unsigned width,unsigned height,size_t pitch,
                             uint32_t gs_frame);
/* Execute caller-owned original menu commands on the same idle queue. The
 * caller has already restored the menu SSBO and exclusively owns this queue.
 * This never copies pixels or edits mapped storage. commands must contain
 * image_count handles; all command/storage resources remain caller-owned.
 * External command arrays use the full queue wait on success. Failure
 * retains/poisons the existing owner. */
int ps2_native_video_execute_commands(Ps2NativeVideo *, const uint64_t *commands);
#ifdef __cplusplus
}
#endif
#endif
