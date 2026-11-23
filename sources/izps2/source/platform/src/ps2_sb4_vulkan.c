#include "ps2_sb4_vulkan.h"

int ps2_sb4_vulkan_bind(const IzNativeApp *app, Ps2Sb4Vulkan *out) {
    if (!out) return 0;
    *out = (Ps2Sb4Vulkan){0};
    if (!sb4ModuleCompat(app)) return 0;
    const IzNativeGraphics *graphics = ((const IzNativeApp6 *)app)->graphics;
    if (!graphics || graphics->version != 1 || graphics->bytes < sizeof(*graphics) ||
        !graphics->instance || !graphics->physical || !graphics->device) return 0;

#define PROC(name) ((PFN_##name)sb4ModuleVulkanProc(app, #name))
    const Ps2Sb4Vulkan binding = {
        .graphics = graphics,
        .get_instance_proc = PROC(vkGetInstanceProcAddr),
        .enumerate_devices = PROC(vkEnumeratePhysicalDevices),
        .memory_properties = PROC(vkGetPhysicalDeviceMemoryProperties),
        .allocate_memory = PROC(vkAllocateMemory),
        .free_memory = PROC(vkFreeMemory),
        .map_memory = PROC(vkMapMemory),
        .unmap_memory = PROC(vkUnmapMemory),
        .create_semaphore = PROC(vkCreateSemaphore),
        .destroy_semaphore = PROC(vkDestroySemaphore),
    };
#undef PROC
    if (!binding.get_instance_proc || !binding.enumerate_devices || !binding.memory_properties ||
        !binding.allocate_memory || !binding.free_memory || !binding.map_memory ||
        !binding.unmap_memory || !binding.create_semaphore || !binding.destroy_semaphore) return 0;
    *out = binding;
    return 1;
}
