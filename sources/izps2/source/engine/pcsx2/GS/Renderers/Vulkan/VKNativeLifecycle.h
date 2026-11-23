#pragma once
// PRIVATE uncompiled SOURCE candidate; no current native/profile ABI changed.
#include "VKLoader.h"
#include <cstdint>
namespace Vulkan {
constexpr uint32_t NativeCreationSchemaV3=3;
struct NativeFeatureValuesV3 {uint32_t type=0,count=0;VkBool32 value[6]{};};
struct NativeQueueRequestV3 {
 uint32_t flags=0,family=0,count=0,priority_bits[2]{};
 uint32_t available_flags=0,available_count=0,timestamp_bits=0;
};
struct NativeQueueResultV3 {uint32_t family=0,index=0;uint64_t handle=0;};
// Frozen before driver create. No feature/name/pNext/queue pointers survive.
struct NativeCreationReceiptV3 {
 uint64_t magic=0x335243564b475350ull;
 uint32_t schema=NativeCreationSchemaV3,bytes=sizeof(NativeCreationReceiptV3);
 uint64_t token[2]{},owner_pthread=0;
 uint64_t borrowed_launcher_instance=0,borrowed_launcher_device=0;
 uint64_t instance_output=0,physical=0,device_output=0;
 uint32_t api_requested=0,api_reported=0,vendor_id=0,device_id=0,driver_version=0;
 uint8_t pipeline_cache_uuid[VK_UUID_SIZE]{};
 char device_name[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE]{};
 uint32_t instance_flags=0,device_flags=0;
 uint32_t shader_cache_disabled=0,precompile_disabled=0,libretro_inactive=0;
 uint32_t instance_extension_count=0,device_extension_count=0;
 char instance_extensions[64][VK_MAX_EXTENSION_NAME_SIZE]{};
 char device_extensions[64][VK_MAX_EXTENSION_NAME_SIZE]{};
 uint32_t instance_layer_count=0,device_layer_count=0;
 char instance_layers[4][VK_MAX_EXTENSION_NAME_SIZE]{};
 char device_layers[4][VK_MAX_EXTENSION_NAME_SIZE]{};
 uint32_t base_features_present=0;
 VkPhysicalDeviceFeatures requested_features{}; // enabled request, NOT support query
 uint32_t feature_count=0;NativeFeatureValuesV3 feature[11]{};
 uint32_t queue_request_count=0;NativeQueueRequestV3 queue_request[3]{};
 uint32_t queue_result_count=0;NativeQueueResultV3 queue_result[6]{};
 uint32_t instance_phase=0,device_phase=0,physical_selection_recorded=0;
 uint32_t instance_driver_called=0,device_driver_called=0;
 int32_t instance_result=0,device_result=0;
 uint32_t uncertain_instance_output=0,uncertain_device_output=0;
 // phase0=none,1=request copied before call,2=raw result/handle published.
 // Request/result values are never reset or rewritten on close/unload.
};
struct NativeWaitReceiptV3 {
 uint64_t sequence=0,device=0,fence=0;
 uint32_t calls=0,stop_seen=0,complete=0;
 int32_t raw_result[50]{};
};
struct NativeQueueDrainV3 {
 uint64_t queue=0,work_epoch=0,sentinel_epoch=0,fence=0;
 int32_t create_result=0,submit_result=0;
 uint32_t created=0,wait_complete=0,destroyed=0;
 NativeWaitReceiptV3 wait{};
};
struct NativeLifetimeReceiptV3 {
 uint64_t submit_epoch=0,idle_epoch=0,drain_sequence=0,wait_sequence=0,drain_snapshot_epoch=0;
 uint64_t last_submit_queue=0;int32_t last_submit_result=0;uint32_t reserved0=0;
 uint64_t regular_submits=0,sentinel_submits=0,sentinel_created=0,sentinel_destroyed=0;
 uint32_t used_queue_count=0,draining=0,instance_destroyed=0,device_destroyed=0;
 NativeQueueDrainV3 drain[6]{};
 NativeWaitReceiptV3 last_wait{};
};
// Same actual private pthread namespace as qualified code-domain provider.
void NativeLifecycleBind(const uint64_t token[2],VkInstance borrowed_instance,VkDevice borrowed_device);
void NativeRequireInitialProfile(bool shader_cache_disabled,bool precompile_disabled,bool libretro_inactive);
VkResult NativeCreateInstanceRecorded(const VkInstanceCreateInfo*,const VkAllocationCallbacks*,VkInstance*);
void NativeRecordPhysicalSelection(VkInstance,uint32_t actual_enumerated_count,VkPhysicalDevice,const VkPhysicalDeviceProperties*);
VkResult NativeCreateDeviceRecorded(VkInstance,VkPhysicalDevice,const VkDeviceCreateInfo*,const VkAllocationCallbacks*,VkDevice*,uint32_t,const VkQueueFamilyProperties*);
void NativeRecordQueue(VkDevice,uint32_t family,uint32_t index,VkQueue);
VkResult NativeSubmitRecorded(VkQueue,uint32_t,const VkSubmitInfo*,VkFence);
VkResult NativeWaitForFence(VkDevice,VkFence);
VkResult NativePollFence(VkDevice,VkFence);
void NativeDrainSubmittedWork(VkDevice);
void NativeRequireIdleEpoch(VkDevice);
void NativeRecordInstanceDestroyed(VkInstance);
void NativeRecordDeviceDestroyed(VkDevice);
// Owner-only copies at explicit call boundaries. No unsynchronized host reader
// and no public runtime export added by this SOURCE stage.
void NativeCopyReceiptsForOwner(NativeCreationReceiptV3*,NativeLifetimeReceiptV3*);
}
