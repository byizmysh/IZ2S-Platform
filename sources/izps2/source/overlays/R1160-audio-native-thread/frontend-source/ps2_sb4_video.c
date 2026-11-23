#include "ps2_sb4_video.h"
#include <sb4/presentation.h>

static int fail(Ps2NativeVideo *video, int stage, VkResult result) {
    video->poisoned = 1;
    video->failure_stage = (uint32_t)stage;
    video->gpu_error = result;
    return stage;
}

static int execute(Ps2NativeVideo *video, const uint64_t *commands, void *context) {
    Ps2Sb4Video *owner = context;
    if (!owner || &owner->video != video || !owner->ticket) return PS2_VIDEO_INVALID;
    const VkAcquireNextImageInfoKHR acquire = {
        .sType = VK_STRUCTURE_TYPE_ACQUIRE_NEXT_IMAGE_INFO_KHR,
        .swapchain = (VkSwapchainKHR)(uintptr_t)video->swapchain,
        .timeout = 100000000,
        .semaphore = (VkSemaphore)(uintptr_t)video->acquire_semaphore,
        .deviceMask = 1,
    };
    uint32_t index = 0;
    VkResult result = owner->acquire((VkDevice)(uintptr_t)video->device, &acquire, &index);
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
        return fail(video, PS2_VIDEO_ACQUIRE, result);
    if (index >= video->image_count) return fail(video, PS2_VIDEO_ACQUIRE, VK_ERROR_UNKNOWN);

    const VkSemaphore complete = (VkSemaphore)(uintptr_t)video->semaphores[index];
    const VkCommandBuffer command = (VkCommandBuffer)(uintptr_t)commands[index];
    const VkFence fence = commands == video->commands ? owner->frame_fence : VK_NULL_HANDLE;
    VkDevice device = (VkDevice)(uintptr_t)video->device;
    const VkPipelineStageFlags stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    const VkSubmitInfo submit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1, .pWaitSemaphores = &acquire.semaphore, .pWaitDstStageMask = &stage,
        .commandBufferCount = 1, .pCommandBuffers = &command,
        .signalSemaphoreCount = 1, .pSignalSemaphores = &complete,
    };
    VkQueue queue = (VkQueue)(uintptr_t)video->queue;
    result = owner->submit(queue, 1, &submit, fence);
    if (result != VK_SUCCESS) return fail(video, PS2_VIDEO_SUBMIT, result);

    const VkPresentInfoKHR present = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1, .pWaitSemaphores = &complete,
        .swapchainCount = 1, .pSwapchains = &acquire.swapchain, .pImageIndices = &index,
    };
    result = owner->present(queue, &present);
    VkResult idle;
    if (fence) {
        // Finish the draw before the CPU rewrites its mapped pixels.
        // Present semaphores follow the image index and its acquire wait.
        idle = owner->wait_fences(device, 1, &fence, VK_TRUE, UINT64_C(5000000000));
        if (idle == VK_SUCCESS) idle = owner->reset_fences(device, 1, &fence);
    } else {
        idle = owner->wait_idle(queue);
    }
    if (idle != VK_SUCCESS) return fail(video, PS2_VIDEO_IDLE, idle);
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
        return fail(video, PS2_VIDEO_PRESENT, result);
    ++video->submissions;
    return PS2_VIDEO_OK;
}

static int valid_target(const Ps2Sb4Video *owner, const Ps2Sb4VideoTarget *target,
                        const IzNativeGraphics *graphics) {
    const VkMemoryPropertyFlags coherent =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    if (!target || !graphics || graphics->version != 1 || graphics->bytes < sizeof(*graphics) ||
        !graphics->device || !graphics->queue || !graphics->swapchain ||
        !target->acquire || !target->mapped || (uintptr_t)target->mapped % 4 ||
        (target->memory_flags & coherent) != coherent ||
        !target->max_width || target->max_width > PS2_VIDEO_MAX_WIDTH ||
        !target->max_height || target->max_height > PS2_VIDEO_MAX_HEIGHT ||
        !target->image_count || target->image_count > 8 ||
        graphics->image_count != target->image_count) return 0;
    size_t required = PS2_VIDEO_HEADER_BYTES + (size_t)target->max_width * target->max_height * 4;
    uintptr_t mapped = (uintptr_t)target->mapped, storage = (uintptr_t)owner;
    if (target->mapped_bytes < required || target->mapped_bytes > UINTPTR_MAX - mapped ||
        (mapped < storage + sizeof(*owner) && storage < mapped + target->mapped_bytes)) return 0;
    for (unsigned i = 0; i < target->image_count; ++i) {
        if (!target->commands[i] || !target->complete[i] || target->complete[i] == target->acquire)
            return 0;
        for (unsigned j = 0; j < i; ++j)
            if (target->commands[i] == target->commands[j] || target->complete[i] == target->complete[j])
                return 0;
    }
    return 1;
}

static void prepare_frame_fence(Ps2Sb4Video *owner, IzNativeApp *app, VkDevice device) {
    PFN_vkCreateFence create_fence = (PFN_vkCreateFence)sb4ModuleVulkanProc(app, "vkCreateFence");
    owner->destroy_fence = (PFN_vkDestroyFence)sb4ModuleVulkanProc(app, "vkDestroyFence");
    owner->wait_fences = (PFN_vkWaitForFences)sb4ModuleVulkanProc(app, "vkWaitForFences");
    owner->reset_fences = (PFN_vkResetFences)sb4ModuleVulkanProc(app, "vkResetFences");
    if (!create_fence || !owner->destroy_fence || !owner->wait_fences || !owner->reset_fences)
        return;
    const VkFenceCreateInfo info = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence fence = VK_NULL_HANDLE;
    if (create_fence(device, &info, NULL, &fence) == VK_SUCCESS) owner->frame_fence = fence;
}

uint32_t ps2_sb4_video_begin(Ps2Sb4Video *owner, IzNativeApp *app, const Ps2Sb4VideoTarget *target) {
    const Sb4Compat *compat = sb4ModuleCompat(app);
    if (!owner || owner->ticket || owner->video.poisoned || !compat || !target)
        return PS2_SB4_VIDEO_INVALID;
    const IzNativeApp6 *native = (const IzNativeApp6 *)app;
    const IzNativeGraphics *graphics = native->graphics;
    if (!graphics || !native->presentation_acquire || !native->presentation_release)
        return PS2_SB4_VIDEO_INVALID;

    Sb4GetPresentationTarget get_target = NULL;
    if (!target->mapped) {
        get_target = (Sb4GetPresentationTarget)compat->resolve_c("sb4GetPresentationTarget");
        if (!get_target) return SB4_PRESENTATION_UNAVAILABLE;
    } else if (!valid_target(owner, target, graphics)) return PS2_SB4_VIDEO_INVALID;

#define PROC(name) ((PFN_##name)sb4ModuleVulkanProc(app, #name))
    Ps2Sb4Video binding = {
        .app = app, .acquire = PROC(vkAcquireNextImage2KHR), .submit = PROC(vkQueueSubmit),
        .present = PROC(vkQueuePresentKHR), .wait_idle = PROC(vkQueueWaitIdle),
    };
#undef PROC
    if (!binding.acquire || !binding.submit || !binding.present || !binding.wait_idle)
        return PS2_SB4_VIDEO_INVALID;
    uint32_t result = native->presentation_acquire(app, &binding.ticket);
    if (result || !binding.ticket) {
        if (binding.ticket) {
            binding.video.poisoned = 1;
            *owner = binding;
            return PS2_SB4_VIDEO_RETAINED;
        }
        return result ? result : PS2_SB4_VIDEO_INVALID;
    }
    const Sb4GetVulkanDeviceProfile get_profile =
        (Sb4GetVulkanDeviceProfile)compat->resolve_c("sb4GetVulkanDeviceProfile");
    if (get_profile && get_profile(app, binding.ticket, &binding.profile, sizeof(binding.profile)))
        binding.profile = (Sb4VulkanDeviceProfile){0};

    Ps2Sb4VideoTarget borrowed = {.max_width = target->max_width, .max_height = target->max_height};
    if (get_target) {
        Sb4PresentationTarget receipt = {0};
        result = get_target(app, binding.ticket, &receipt);
        if (!result && (receipt.version != 1 || receipt.bytes < sizeof(receipt) ||
                        receipt.reserved || receipt.image_count > SB4_PRESENTATION_IMAGES ||
                        receipt.mapped_bytes > SIZE_MAX)) result = PS2_SB4_VIDEO_INVALID;
        if (!result) {
            borrowed.acquire = (VkSemaphore)(uintptr_t)receipt.acquire;
            borrowed.mapped = receipt.mapped;
            borrowed.mapped_bytes = (size_t)receipt.mapped_bytes;
            borrowed.memory_flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            borrowed.image_count = receipt.image_count;
            for (unsigned i = 0; i < receipt.image_count; ++i) {
                borrowed.commands[i] = (VkCommandBuffer)(uintptr_t)receipt.commands[i];
                borrowed.complete[i] = (VkSemaphore)(uintptr_t)receipt.complete[i];
            }
            if (!valid_target(owner, &borrowed, graphics)) result = PS2_SB4_VIDEO_INVALID;
        }
        if (result) {
            if (!native->presentation_release(app, binding.ticket)) return result;
            binding.video.poisoned = 1;
            *owner = binding;
            return PS2_SB4_VIDEO_RETAINED;
        }
        target = &borrowed;
    }
    binding.video = (Ps2NativeVideo){
        .device = graphics->device, .queue = graphics->queue, .swapchain = graphics->swapchain,
        .acquire_semaphore = (uintptr_t)target->acquire, .mapped = target->mapped,
        .mapped_bytes = target->mapped_bytes, .image_count = target->image_count,
        .max_width = target->max_width, .max_height = target->max_height,
        .execute_queue = execute, .queue_context = owner,
    };
    for (unsigned i = 0; i < target->image_count; ++i) {
        binding.video.commands[i] = (uintptr_t)target->commands[i];
        binding.video.semaphores[i] = (uintptr_t)target->complete[i];
    }
    prepare_frame_fence(&binding, app, (VkDevice)(uintptr_t)graphics->device);
    *owner = binding;
    return 0;
}

uint32_t ps2_sb4_video_finish(Ps2Sb4Video *owner) {
    if (!owner) return PS2_SB4_VIDEO_INVALID;
    if (owner->video.poisoned) return PS2_SB4_VIDEO_RETAINED;
    if (!owner->ticket) return 0;
    const IzNativeApp6 *native = (const IzNativeApp6 *)owner->app;
    uint32_t result = native->presentation_release(owner->app, owner->ticket);
    if (!result) {
        if (owner->frame_fence)
            owner->destroy_fence((VkDevice)(uintptr_t)owner->video.device, owner->frame_fence, NULL);
        *owner = (Ps2Sb4Video){0};
    }
    return result;
}
