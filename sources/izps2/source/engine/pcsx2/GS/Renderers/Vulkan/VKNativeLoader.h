#pragma once
/* Uncompiled native-only SOURCE implementation contract. */
#include "VKLoader.h"
#include <cstdint>

namespace Vulkan
{
 // Called once on CPU owner before starting GS. The actual typed GIPA must
 // come from independently admitted HBXL dispatch, never an address guess.
 // This does not prove any enabled Launcher feature and never borrows its device.
 bool NativeBindExternalGipa(PFN_vkGetInstanceProcAddr, const uint64_t token[2],
                            VkInstance forbidden_instance, VkDevice forbidden_device);
 bool NativeLoaderBound();
 // Only actual successful create/destroy sites may call these. A failed/unknown
 // creation/join/drain must retain the entire native owner before freeing anything.
 void NativeNoteInstanceCreated(VkInstance);
 void NativeNoteDeviceCreated(VkInstance, VkDevice);
 void NativeBeforeDestroy();
 void NativeNoteDeviceDestroyed(VkDevice);
 void NativeNoteInstanceDestroyed(VkInstance);
}

// Intentionally STRONG and without a success fallback. No native link may be
// admitted until this is implemented in the real AV retained/park owner, tested
// before any destructor/free, and its SDK exit policy reviewed independently.
extern "C" __attribute__((visibility("hidden"),noreturn))
void ps2_hwgs_retain_ownership(const char* stage, int32_t raw_result);
