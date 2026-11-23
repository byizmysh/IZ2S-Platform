#ifndef SB4_VULKAN_DEVICE_H
#define SB4_VULKAN_DEVICE_H

#include "presentation.h"
#include <vulkan/vulkan_core.h>

enum { SB4_VULKAN_DEVICE_PROFILE_VERSION = 1 };

/* Enabled settings of the borrowed device in IzNativeGraphics. The core
 * features are the VkPhysicalDeviceFeatures used at device creation. */
typedef struct {
    uint32_t version, bytes, instance_api_version, queue_family_index;
    VkPhysicalDeviceFeatures core_features;
    VkBool32 synchronization2, dynamic_rendering, maintenance6;
} Sb4VulkanDeviceProfile;

/* Optional resolve_c import "sb4GetVulkanDeviceProfile". The application
 * supplies the size of its output buffer. Query during the serialized
 * callback after presentation_acquire and before worker_begin. The copy
 * describes the device until presentation_release. */
typedef uint32_t (*Sb4GetVulkanDeviceProfile)(struct IzNativeApp *, uint64_t ticket,
                                              Sb4VulkanDeviceProfile *, uint32_t bytes);

#endif
