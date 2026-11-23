#ifndef IZ_OUNCE_VULKAN_EXTRA_H
#define IZ_OUNCE_VULKAN_EXTRA_H

#include <vulkan/vulkan.h>

PFN_vkVoidFunction iz_ounce_vulkan_query(VkInstance instance, PFN_vkGetInstanceProcAddr lookup,
                                       const char *name);
PFN_vkVoidFunction iz_ounce_vulkan_resource(VkDevice device, PFN_vkGetDeviceProcAddr lookup,
                                          const char *name);

#endif
