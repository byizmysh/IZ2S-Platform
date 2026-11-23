#ifndef PS2_SB4_VULKAN_H
#define PS2_SB4_VULKAN_H

#include <sb4/native-app.h>
#include <vulkan/vulkan_core.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Borrowed Vulkan device and entry points. PS2 creates and destroys its own
 * memory and semaphores. Presentation still requires the launcher lease. */
typedef struct {
    const IzNativeGraphics *graphics;
    PFN_vkGetInstanceProcAddr get_instance_proc;
    PFN_vkEnumeratePhysicalDevices enumerate_devices;
    PFN_vkGetPhysicalDeviceMemoryProperties memory_properties;
    PFN_vkAllocateMemory allocate_memory;
    PFN_vkFreeMemory free_memory;
    PFN_vkMapMemory map_memory;
    PFN_vkUnmapMemory unmap_memory;
    PFN_vkCreateSemaphore create_semaphore;
    PFN_vkDestroySemaphore destroy_semaphore;
} Ps2Sb4Vulkan;

/* Returns one on success and clears the output on failure. */
int ps2_sb4_vulkan_bind(const IzNativeApp *app, Ps2Sb4Vulkan *out);

#ifdef __cplusplus
}
#endif
#endif
