#ifndef PS2_SB4_VIDEO_H
#define PS2_SB4_VIDEO_H

#include "native_video.h"
#include "ps2_sb4_vulkan.h"
#include <sb4/vulkan-device.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    VkSemaphore acquire;
    VkSemaphore complete[8];
    VkCommandBuffer commands[8];
    void *mapped;
    size_t mapped_bytes;
    VkMemoryPropertyFlags memory_flags;
    uint32_t image_count, max_width, max_height;
} Ps2Sb4VideoTarget;

typedef struct {
    Ps2NativeVideo video;
    IzNativeApp *app;
    uint64_t ticket;
    PFN_vkAcquireNextImage2KHR acquire;
    PFN_vkQueueSubmit submit;
    PFN_vkQueuePresentKHR present;
    PFN_vkQueueWaitIdle wait_idle;
    Sb4VulkanDeviceProfile profile;
    VkFence frame_fence;
    PFN_vkDestroyFence destroy_fence;
    PFN_vkWaitForFences wait_fences;
    PFN_vkResetFences reset_fences;
} Ps2Sb4Video;

enum { PS2_SB4_VIDEO_INVALID = 0xe8e0, PS2_SB4_VIDEO_RETAINED = 0xe8e1 };

/* Zero-initialize the owner and keep it at a stable address. Invoke in the
 * serialized module callback after the engine image is ready and before
 * starting its worker. Supply app-owned Vulkan resources, or leave mapped
 * NULL and supply only the frame dimensions to borrow the launcher's target. */
uint32_t ps2_sb4_video_begin(Ps2Sb4Video *owner, IzNativeApp *app, const Ps2Sb4VideoTarget *target);
/* After the worker joins, release the queue. The app can then destroy any
 * resources it supplied. On failure presentation resources remain live. */
uint32_t ps2_sb4_video_finish(Ps2Sb4Video *owner);

#ifdef __cplusplus
}
#endif
#endif
