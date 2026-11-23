// SPDX-License-Identifier: GPL-3.0+
#include "video.h"
#include "ps2_present_cadence.h"
#include "common/Timer.h"
#ifdef IZPS2_NATIVE_FATAL_RECORD
#endif
#ifdef __SWITCH2__
#include "native-system.h"
#endif
#include "native_video.h"
#include "sw_frame_mode.h"
#include "sw_presentation.h"
#include "sw_temporal_storage.h"
#include "Host.h"
#include "GS/GS.h"
#include "GS/Renderers/SW/GSRendererSW.h"
#ifdef ENABLE_VULKAN
#include "common/Console.h"
#include "common/Threading.h"
#include "GS/Renderers/Common/GSHeadlessPresent.h"
#include "GS/Renderers/Vulkan/GSDeviceVK.h"
#include "ps2_timing_windows.hpp"
#include <sb4/native-app.h>
#include <sb4/presentation.h>
#if defined(IZPS2_SWITCH2_FRAME_SAMPLES) || defined(IZPS2_SWITCH2_BOUNDED_PRESENT) || \
    defined(IZPS2_SWITCH2_PRESENT_ISOLATION)
#include "ps2_sb4_layout.h"
#include "common/FileSystem.h"
#endif
#ifdef IZPS2_SWITCH2_FRAME_SAMPLES
#include "ps2_video_sample_history.hpp"
#include <sb4.h>
#include <sb4/storage.h>
#endif
#include <array>
#endif
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
constexpr unsigned width = 960, height = 540;
constexpr size_t scratch_bytes = 16 * 1024 * 1024;
void* scratch = nullptr;
uint8_t* output = nullptr;
Ps2NativeVideo* sb4_output = nullptr;
uint32_t (*sb4_input_focused)() = nullptr;
IzNroPresent nro_present = nullptr;
bool (*nro_input_focused)() = nullptr;
Ps2SwFrameModeHistory history{};
bool failed = false;
uint64_t frames = 0;
std::atomic<uint64_t> accepted_presentations{0};
std::atomic<unsigned> requested_frame_rate{0};
// The GS thread owns the cadence. The VM thread publishes only the setting.
unsigned applied_frame_rate = 0;
Ps2PresentCadence cadence;

bool frame_due() {
    const unsigned rate = requested_frame_rate.load(std::memory_order_relaxed);
    if (rate != applied_frame_rate) {
        cadence.Configure(Common::Timer::ConvertSecondsToValue(1), rate);
        applied_frame_rate = rate;
    }
    return !rate || cadence.Due(Common::Timer::GetCurrentValue());
}

void frame_accepted() {
    if (cadence.Enabled())
        cadence.Accepted(Common::Timer::GetCurrentValue());
    accepted_presentations.fetch_add(1, std::memory_order_relaxed);
}
void fail(const char* message) {
    if (failed) return;
    failed = true;
    Host::ReportErrorAsync("PS2 video", message);
    Host::RequestVMShutdown(false, false, false);
}

#ifdef ENABLE_VULKAN
// The GS thread is the only queue producer while libsb4 holds presentation.
// The launcher joins it before reclaiming the queue and its draw commands.
struct VulkanPresentation {
    struct Slot {
        VkSemaphore acquire = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
        bool pending = false;
    };

    VkBuffer buffer = VK_NULL_HANDLE;
    std::array<Slot, 3> slots{};
    PFN_vkAcquireNextImage2KHR acquire = nullptr;
    PFN_vkQueuePresentKHR present = nullptr;
    PFN_vkDestroySemaphore destroy_semaphore = nullptr;
    PFN_vkDestroyFence destroy_fence = nullptr;
    PFN_vkQueueWaitIdle wait_idle = nullptr;
    unsigned next_slot = 0;
    bool invalid_image = false;
    bool unconsumed_acquire = false;
    uint64_t recorded_serial = 0, presented_serial = 0, frames = 0;
    uint64_t acquire_timeouts = 0, fence_waits = 0;
#ifdef IZPS2_SWITCH2_BOUNDED_PRESENT
    bool bounded_acquire = false;
    uint64_t busy_slot_deferrals = 0;
#endif
#ifdef IZPS2_SWITCH2_PRESENT_ISOLATION
    bool sparse_present = false;
    uint64_t isolation_skips = 0;
    Common::Timer::Value draw_submit_ticks = 0, queue_present_ticks = 0;
#endif
    Common::Timer::Value acquire_ticks = 0, fence_wait_ticks = 0;
    Common::Timer::Value first_frame_wall = 0, last_frame_wall = 0;
};
#ifdef IZPS2_SWITCH2_FRAME_SAMPLES
struct VulkanFrameSamples
{
    // Raw Vulkan allocation on the borrowed device. The renderer's Vulkan
    // allocator may already be closed by the time the presenter is released.
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    const uint8_t* mapped = nullptr;
    PFN_vkDestroyBuffer destroy = nullptr;
    PFN_vkFreeMemory free = nullptr;
    PFN_vkUnmapMemory unmap = nullptr;
    Ps2VideoSampleHistory history{};
};
VulkanFrameSamples frame_samples{};
constexpr auto sample_bytes = sizeof(Ps2VideoSampleHistory::Samples);

// record_vulkan_frame has already checked the image extent and format.
// Both the sample positions and the eight ring offsets can live in .rodata.
constexpr unsigned sample_grid = 8;
consteval auto build_frame_sample_regions()
{
    using History = Ps2VideoSampleHistory;
    static_assert(History::Pixels == sample_grid * sample_grid);
    std::array<std::array<VkBufferImageCopy, History::Pixels>, History::SlotCount> table{};
    for (size_t slot = 0; slot < table.size(); ++slot) {
        for (size_t i = 0; i < table[slot].size(); ++i) {
            const auto x = i % sample_grid;
            const auto y = i / sample_grid;
            auto& copy = table[slot][i];
            copy.bufferOffset = slot * sample_bytes + i * sizeof(uint32_t);
            copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copy.imageOffset = {static_cast<int32_t>((2 * x + 1) * width / (2 * sample_grid)),
                                static_cast<int32_t>((2 * y + 1) * height / (2 * sample_grid)), 0};
            copy.imageExtent = {1, 1, 1};
        }
    }
    return table;
}
constexpr auto frame_sample_regions = build_frame_sample_regions();
static_assert(frame_sample_regions.front().front().imageOffset.x == 60);
static_assert(frame_sample_regions.front().front().imageOffset.y == 33);
static_assert(frame_sample_regions.back().back().imageOffset.x == 900);
static_assert(frame_sample_regions.back().back().imageOffset.y == 506);
static_assert(frame_sample_regions.back().back().bufferOffset + sizeof(uint32_t) ==
              sample_bytes * Ps2VideoSampleHistory::SlotCount);


void release_frame_samples(VkDevice device)
{
    if (frame_samples.mapped) frame_samples.unmap(device, frame_samples.memory);
    if (frame_samples.buffer) frame_samples.destroy(device, frame_samples.buffer, nullptr);
    if (frame_samples.memory) frame_samples.free(device, frame_samples.memory, nullptr);
    frame_samples = {};
}

template<typename GetProc>
void initialize_frame_samples(VkPhysicalDevice physical, VkDevice device,
                              PFN_vkGetPhysicalDeviceMemoryProperties get_properties, const GetProc& proc)
{
    const auto create = reinterpret_cast<PFN_vkCreateBuffer>(proc("vkCreateBuffer"));
    const auto requirements = reinterpret_cast<PFN_vkGetBufferMemoryRequirements>(proc("vkGetBufferMemoryRequirements"));
    const auto allocate = reinterpret_cast<PFN_vkAllocateMemory>(proc("vkAllocateMemory"));
    const auto bind = reinterpret_cast<PFN_vkBindBufferMemory>(proc("vkBindBufferMemory"));
    const auto map = reinterpret_cast<PFN_vkMapMemory>(proc("vkMapMemory"));
    frame_samples.destroy = reinterpret_cast<PFN_vkDestroyBuffer>(proc("vkDestroyBuffer"));
    frame_samples.free = reinterpret_cast<PFN_vkFreeMemory>(proc("vkFreeMemory"));
    frame_samples.unmap = reinterpret_cast<PFN_vkUnmapMemory>(proc("vkUnmapMemory"));
    if (!physical || !get_properties || !create || !requirements || !allocate || !bind || !map ||
        !frame_samples.destroy || !frame_samples.free || !frame_samples.unmap)
        return;

    VkBufferCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size = sample_bytes * Ps2VideoSampleHistory::SlotCount;
    info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (create(device, &info, nullptr, &frame_samples.buffer) != VK_SUCCESS)
        return;
    VkMemoryRequirements req{};
    requirements(device, frame_samples.buffer, &req);
    VkPhysicalDeviceMemoryProperties props{};
    get_properties(physical, &props);
    constexpr VkMemoryPropertyFlags flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    uint32_t index = 0;
    for (; index < props.memoryTypeCount; ++index)
        if ((req.memoryTypeBits & (1u << index)) && (props.memoryTypes[index].propertyFlags & flags) == flags)
            break;
    if (index == props.memoryTypeCount) {
        release_frame_samples(device);
        return;
    }

    VkMemoryAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = index;
    if (allocate(device, &alloc, nullptr, &frame_samples.memory) != VK_SUCCESS ||
        bind(device, frame_samples.buffer, frame_samples.memory, 0) != VK_SUCCESS) {
        release_frame_samples(device);
        return;
    }
    void* mapped = nullptr;
    if (map(device, frame_samples.memory, 0, info.size, 0, &mapped) != VK_SUCCESS) {
        release_frame_samples(device);
        return;
    }
    frame_samples.mapped = static_cast<const uint8_t*>(mapped);
    frame_samples.history.StartChangeWindows();
}

void drain_frame_samples(uint64_t completed)
{
    frame_samples.history.Drain(completed, [](size_t slot, auto& pixels) {
        std::memcpy(pixels.data(), frame_samples.mapped + slot * sample_bytes, sample_bytes);
    });
}

void record_frame_samples(VkCommandBuffer command, VkImage image, uint64_t serial)
{
    auto* const gs = GSDeviceVK::GetInstance();
    drain_frame_samples(gs->GetCompletedFenceCounter());
    const int slot = frame_samples.history.Record(serial, gs->GetCurrentFenceCounter());
    if (slot < 0)
        return;

    const auto& regions = frame_sample_regions[slot];
    const auto destination = regions.front().bufferOffset;
    vkCmdCopyImageToBuffer(command, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, frame_samples.buffer,
                           static_cast<uint32_t>(regions.size()), regions.data());
    VkBufferMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    barrier.buffer = frame_samples.buffer;
    barrier.offset = destination;
    barrier.size = sample_bytes;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr,
                         1, &barrier, 0, nullptr);
}
#endif
VulkanPresentation vulkan_output{};
Ps2TimingWindows<uint64_t> presentation_windows;
#ifdef __SWITCH2__
bool gs_affinity_configured = false;
#endif
constexpr VkDeviceSize frame_bytes = PS2_VIDEO_HEADER_BYTES + VkDeviceSize(width) * height * 4;

void destroy_vulkan_slots(VulkanPresentation& target, VkDevice device) {
    for (const auto& slot : target.slots) {
        if (slot.acquire) target.destroy_semaphore(device, slot.acquire, nullptr);
        if (slot.fence) target.destroy_fence(device, slot.fence, nullptr);
    }
}

#ifdef IZPS2_SWITCH2_PRESENT_ISOLATION
constexpr unsigned isolation_interval = 8;

bool isolate_presentation(VulkanPresentation& target, uint64_t serial)
{
    // Keep the first successful image and periodic live previews. No Vulkan
    // operation is recorded for the omitted scanouts. The GS still renders
    // and submits its own work, with its original command-buffer fences.
    if (!target.sparse_present || !target.frames || serial % isolation_interval == 0)
        return false;
    ++target.isolation_skips;
    return true;
}
#endif

void record_vulkan_frame(VkCommandBuffer command, VkImage image, VkFormat format,
                         uint32_t w, uint32_t h, uint64_t serial) {
    if (vulkan_output.invalid_image) return;
    if (format != VK_FORMAT_R8G8B8A8_UNORM || w != width || h != height) {
        std::fprintf(stderr, "IZPS2_VIDEO_ERROR incompatible_image format=%d extent=%ux%u\n",
            int(format), w, h);
        vulkan_output.invalid_image = true;
        return;
    }
#ifdef IZPS2_SWITCH2_PRESENT_ISOLATION
    // Leaving recorded_serial untouched makes the presentation callback
    // reject this serial, even if an earlier Vulkan acquire was deferred.
    if (isolate_presentation(vulkan_output, serial))
        return;
#endif
    // Skip the scanout copy and the WSI submission together. The GS still
    // runs the guest's render work and its own command-buffer finalization.
    if (!frame_due())
        return;
    constexpr VkPipelineStageFlags shaders = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    VkBufferMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = vulkan_output.buffer;
    barrier.size = frame_bytes;
    // Complete earlier draws and any undisplayed copies before reusing the single buffer.
    barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(command, shaders | VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 1, &barrier, 0, nullptr);

    const uint32_t header[] = {w, h, static_cast<uint32_t>(serial)};
    vkCmdUpdateBuffer(command, vulkan_output.buffer, 0, sizeof(header), header);
    VkBufferImageCopy region{};
    region.bufferOffset = PS2_VIDEO_HEADER_BYTES;
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {w, h, 1};
    vkCmdCopyImageToBuffer(command, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, vulkan_output.buffer, 1, &region);

    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, shaders,
        0, 0, nullptr, 1, &barrier, 0, nullptr);
#ifdef IZPS2_SWITCH2_FRAME_SAMPLES
    if (frame_samples.mapped)
        record_frame_samples(command, image, serial);
#endif
    vulkan_output.recorded_serial = serial;
}

void report_vulkan_error(unsigned stage, VkResult result) {
    sb4_output->poisoned = 1;
    sb4_output->failure_stage = stage;
    sb4_output->gpu_error = result;
    std::fprintf(stderr, "IZPS2_VIDEO_ERROR stage=%u vk_result=%d\n", stage, int(result));
    fail("PS2 Vulkan output failed.");
}

void present_vulkan_frame() {
    auto& target = vulkan_output;
    if (target.invalid_image) {
        fail("PS2 Vulkan output received an incompatible image.");
        return;
    }
    const uint64_t serial = target.recorded_serial;
#ifdef IZPS2_NATIVE_FATAL_RECORD
    const bool startup_trace = target.frames < 4 && iz_ounce_native_trace_enabled();
    if (startup_trace)
        Console.WriteLn("IZPS2_VIDEO_STARTUP phase=present serial=%llu recorded=%llu current=%llu",
            static_cast<unsigned long long>(serial), static_cast<unsigned long long>(target.presented_serial),
            static_cast<unsigned long long>(GSHeadless::PresentCount()));
#endif
    if (target.presented_serial == serial || serial != GSHeadless::PresentCount() ||
        !GSDeviceVK::GetInstance()->IsBorrowedFrameCurrent(serial)) return;

    const auto device = reinterpret_cast<VkDevice>(sb4_output->device);
    const auto queue = reinterpret_cast<VkQueue>(sb4_output->queue);
#ifdef IZPS2_SWITCH2_BOUNDED_PRESENT
    // Keep the first present's original acquisition behavior. Once an
    // image is visible, a busy WSI slot must not park the GS thread.
    const bool bounded = target.bounded_acquire && target.frames != 0;
#else
    constexpr bool bounded = false;
#endif
    auto& slot = target.slots[target.next_slot];
    if (slot.pending) {
        VkResult result = vkGetFenceStatus(device, slot.fence);
        if (result == VK_NOT_READY) {
#ifdef IZPS2_SWITCH2_BOUNDED_PRESENT
            if (bounded) {
                ++target.busy_slot_deferrals;
                return;
            }
#endif
            const auto start = Common::Timer::GetCurrentValue();
            result = vkWaitForFences(device, 1, &slot.fence, VK_TRUE, UINT64_C(5000000000));
            target.fence_wait_ticks += Common::Timer::GetCurrentValue() - start;
            ++target.fence_waits;
        }
        if (result == VK_SUCCESS) result = vkResetFences(device, 1, &slot.fence);
        if (result != VK_SUCCESS) {
            report_vulkan_error(PS2_VIDEO_IDLE, result);
            return;
        }
        slot.pending = false;
    }

    VkAcquireNextImageInfoKHR acquire{};
    acquire.sType = VK_STRUCTURE_TYPE_ACQUIRE_NEXT_IMAGE_INFO_KHR;
    acquire.swapchain = reinterpret_cast<VkSwapchainKHR>(sb4_output->swapchain);
    acquire.timeout = bounded ? UINT64_C(2000000) : UINT64_C(100000000);
    acquire.semaphore = slot.acquire;
    acquire.deviceMask = 1;
    uint32_t index = 0;
    const auto start = Common::Timer::GetCurrentValue();
    VkResult result = target.acquire(device, &acquire, &index);
#ifdef IZPS2_NATIVE_FATAL_RECORD
    if (startup_trace)
        Console.WriteLn("IZPS2_VIDEO_STARTUP phase=acquire result=%d index=%u", int(result), index);
#endif
    target.acquire_ticks += Common::Timer::GetCurrentValue() - start;
    if (result == VK_TIMEOUT || result == VK_NOT_READY) {
        ++target.acquire_timeouts;
        return;
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        report_vulkan_error(PS2_VIDEO_ACQUIRE, result);
        return;
    }
    target.unconsumed_acquire = true;
    if (index >= sb4_output->image_count) {
        report_vulkan_error(PS2_VIDEO_ACQUIRE, VK_ERROR_UNKNOWN);
        return;
    }
    // The present semaphore belongs to the swapchain image. Reacquiring that
    // image and waiting on the acquire semaphore orders its previous present.
    const auto complete = reinterpret_cast<VkSemaphore>(sb4_output->semaphores[index]);
    const auto draw = reinterpret_cast<VkCommandBuffer>(sb4_output->commands[index]);
    const VkPipelineStageFlags stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &slot.acquire;
    submit.pWaitDstStageMask = &stage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &draw;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &complete;
#ifdef IZPS2_SWITCH2_PRESENT_ISOLATION
    const auto draw_start = Common::Timer::GetCurrentValue();
#endif
    result = vkQueueSubmit(queue, 1, &submit, slot.fence);
#ifdef IZPS2_NATIVE_FATAL_RECORD
    if (startup_trace)
        Console.WriteLn("IZPS2_VIDEO_STARTUP phase=submit result=%d", int(result));
#endif
#ifdef IZPS2_SWITCH2_PRESENT_ISOLATION
    target.draw_submit_ticks += Common::Timer::GetCurrentValue() - draw_start;
#endif
    if (result != VK_SUCCESS) {
        report_vulkan_error(PS2_VIDEO_SUBMIT, result);
        return;
    }
    target.unconsumed_acquire = false;
    slot.pending = true;
    target.next_slot = (target.next_slot + 1) % target.slots.size();

    VkPresentInfoKHR present{};
    present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &complete;
    present.swapchainCount = 1;
    present.pSwapchains = &acquire.swapchain;
    present.pImageIndices = &index;
#ifdef IZPS2_SWITCH2_PRESENT_ISOLATION
    const auto present_start = Common::Timer::GetCurrentValue();
#endif
    result = target.present(queue, &present);
#ifdef IZPS2_NATIVE_FATAL_RECORD
    if (startup_trace)
        Console.WriteLn("IZPS2_VIDEO_STARTUP phase=presented result=%d", int(result));
#endif
#ifdef IZPS2_SWITCH2_PRESENT_ISOLATION
    target.queue_present_ticks += Common::Timer::GetCurrentValue() - present_start;
#endif
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        report_vulkan_error(PS2_VIDEO_PRESENT, result);
        return;
    }
    target.presented_serial = serial;
    target.last_frame_wall = Common::Timer::GetCurrentValue();
#ifdef IZPS2_SWITCH2_FRAME_SAMPLES
    if (frame_samples.mapped)
        frame_samples.history.Accept(serial, target.last_frame_wall);
#endif
    if (!target.frames)
        target.first_frame_wall = target.last_frame_wall;
    ++target.frames;
    presentation_windows.Add(static_cast<uint32_t>(target.frames), target.last_frame_wall,
        [] { return Threading::GetThreadCpuTime(); });
    ++sb4_output->submissions;
    if (cadence.Enabled())
        cadence.Accepted(target.last_frame_wall);
    accepted_presentations.fetch_add(1, std::memory_order_relaxed);
}
#endif

int present(uint32_t frame) {
    const int result = sb4_output
        ? ps2_native_video_present(sb4_output, output, width * height * 4, width, height, width * 4, frame)
        : nro_present(output, width, height, width * 4);
    if (!result) frame_accepted();
    return result;
}

bool initialize_buffers() {
    if (scratch || output) return false;
    if (posix_memalign(&scratch, 64, scratch_bytes)) return false;
    output = static_cast<uint8_t*>(std::calloc(width * height, 4));
    if (!output) { std::free(scratch); scratch = nullptr; return false; }
    history = {}; failed = false; frames = 0;
    return true;
}
}

bool iz_video_initialize_nro(bool (*input_focused)(), IzNroPresent present_frame) {
    if (!input_focused || !present_frame || !initialize_buffers()) return false;
    nro_input_focused = input_focused;
    nro_present = present_frame;
    return true;
}

bool iz_video_initialize(Ps2NativeVideo* target, uint32_t (*input_focused)()) {
    if (!target || !input_focused || !target->mapped || target->poisoned ||
        target->max_width < width || target->max_height < height || !initialize_buffers()) return false;
    sb4_output = target;
    sb4_input_focused = input_focused;
    return true;
}
#ifdef ENABLE_VULKAN
Vulkan::RecordBorrowedFrame iz_video_attach_vulkan(IzNativeApp* app, VkInstance instance,
                                                    PFN_vkGetInstanceProcAddr lookup) {
    const auto* compat = sb4ModuleCompat(app);
    const auto get_buffer = reinterpret_cast<Sb4GetPresentationBuffer>(
        compat->resolve_c("sb4GetPresentationBuffer"));
    uint64_t buffer = 0;
    if (!get_buffer || sb4_output->mapped_bytes < frame_bytes ||
        get_buffer(app, sb4_output->mapped, &buffer) || !buffer) return nullptr;

    const auto device = reinterpret_cast<VkDevice>(sb4_output->device);
    const auto device_lookup = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
        lookup(instance, "vkGetDeviceProcAddr"));
    if (!device_lookup) return nullptr;
    const auto proc = [&](const char* name) { return device_lookup(device, name); };
    VulkanPresentation target{};
    target.buffer = reinterpret_cast<VkBuffer>(buffer);
    target.acquire = reinterpret_cast<PFN_vkAcquireNextImage2KHR>(proc("vkAcquireNextImage2KHR"));
    target.present = reinterpret_cast<PFN_vkQueuePresentKHR>(proc("vkQueuePresentKHR"));
    target.destroy_semaphore = reinterpret_cast<PFN_vkDestroySemaphore>(proc("vkDestroySemaphore"));
    target.destroy_fence = reinterpret_cast<PFN_vkDestroyFence>(proc("vkDestroyFence"));
    target.wait_idle = reinterpret_cast<PFN_vkQueueWaitIdle>(proc("vkQueueWaitIdle"));
    const auto create_semaphore = reinterpret_cast<PFN_vkCreateSemaphore>(proc("vkCreateSemaphore"));
    const auto create_fence = reinterpret_cast<PFN_vkCreateFence>(proc("vkCreateFence"));
    if (!target.acquire || !target.present || !target.wait_idle || !target.destroy_semaphore ||
        !target.destroy_fence || !create_semaphore || !create_fence) return nullptr;
    const VkSemaphoreCreateInfo semaphore_info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    const VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    for (auto& slot : target.slots) {
        VkSemaphore acquire = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
        VkResult result = create_semaphore(device, &semaphore_info, nullptr, &acquire);
        if (result == VK_SUCCESS) {
            slot.acquire = acquire;
            result = create_fence(device, &fence_info, nullptr, &fence);
        }
        if (result != VK_SUCCESS) {
            destroy_vulkan_slots(target, device);
            std::fprintf(stderr, "IZPS2_VIDEO_PATH mode=readback setup_vk_result=%d\n", int(result));
            return nullptr;
        }
        slot.fence = fence;
    }
#ifdef IZPS2_SWITCH2_FRAME_SAMPLES
    const auto* storage = sb4ModuleStorage(app);
    uint64_t flag_bytes = 0;
    if (storage && storage->file_size && storage->file_size(app, "frame-pixels.flag", &flag_bytes) == 0) {
        const auto props = reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(
            lookup(instance, "vkGetPhysicalDeviceMemoryProperties"));
        const auto* const graphics = reinterpret_cast<const IzNativeApp6*>(app)->graphics;
        if (graphics)
            initialize_frame_samples(reinterpret_cast<VkPhysicalDevice>(graphics->physical), device, props, proc);
        Console.WriteLn("IZPS2_FRAME_SAMPLE_CONFIG enabled=%d pixels=%zu slots=%zu",
            frame_samples.mapped ? 1 : 0, Ps2VideoSampleHistory::Pixels, Ps2VideoSampleHistory::SlotCount);
    }
#endif
#ifdef IZPS2_SWITCH2_BOUNDED_PRESENT
    target.bounded_acquire = FileSystem::FileExists("sdmc:" PS2_SB4_SD_ROOT "/present-bounded.flag");
#endif
#ifdef IZPS2_SWITCH2_PRESENT_ISOLATION
    target.sparse_present = FileSystem::FileExists("sdmc:" PS2_SB4_SD_ROOT "/present-isolation.flag");
    std::fprintf(stderr, "IZPS2_PRESENT_ISOLATION_START enabled=%d interval=%u\n",
        target.sparse_present ? 1 : 0, isolation_interval);
#endif
    vulkan_output = target;
    presentation_windows.Start();
    std::fprintf(stderr, "IZPS2_VIDEO_PATH mode=gpu_copy slots=%zu extent=%ux%u\n",
        target.slots.size(), width, height);
    return record_vulkan_frame;
}
#endif

bool iz_video_has_frame_samples() {
#ifdef IZPS2_SWITCH2_FRAME_SAMPLES
    return frame_samples.mapped != nullptr;
#else
    return false;
#endif
}

void iz_video_finish_presentations() {
    // The caller must have joined MTGS before touching its presentation data.
#ifdef ENABLE_VULKAN
    if (vulkan_output.buffer) {
        const auto device = reinterpret_cast<VkDevice>(sb4_output->device);
        const VkResult result = vulkan_output.wait_idle(reinterpret_cast<VkQueue>(sb4_output->queue));
        if (result != VK_SUCCESS) report_vulkan_error(PS2_VIDEO_IDLE, result);
        if (result == VK_SUCCESS && !vulkan_output.unconsumed_acquire) destroy_vulkan_slots(vulkan_output, device);
#ifdef IZPS2_SWITCH2_FRAME_SAMPLES
        if (frame_samples.mapped) {
            if (result == VK_SUCCESS)
                drain_frame_samples(UINT64_MAX);
            const auto& samples = frame_samples.history;
            const auto& windows = samples.GetChangeWindows();
            for (size_t i = 0; i < windows.Count(); ++i) {
                const auto& window = windows.Get(i);
                const auto [p50, p95, p99] = window.Percentiles();
                Console.WriteLn(
                    "IZPS2_SAMPLE_CHANGE_WINDOW start_change=%u wall_start_tick=%llu wall_ms=%.3f "
                    "change_intervals=%zu accepted_intervals=%llu "
                    "change_p50_ms=%.3f change_p95_ms=%.3f change_p99_ms=%.3f change_max_ms=%.3f",
                    window.first_frame, static_cast<unsigned long long>(window.first_tick),
                    Common::Timer::ConvertValueToMilliseconds(window.elapsed), windows.Intervals,
                    static_cast<unsigned long long>(window.last - window.first),
                    Common::Timer::ConvertValueToMilliseconds(p50),
                    Common::Timer::ConvertValueToMilliseconds(p95),
                    Common::Timer::ConvertValueToMilliseconds(p99),
                    Common::Timer::ConvertValueToMilliseconds(window.MaxInterval()));
            }
            const double elapsed_ms = Common::Timer::ConvertValueToMilliseconds(samples.compared_ticks);
            Console.WriteLn("IZPS2_FRAME_SAMPLES accepted=%llu sampled=%llu consecutive_pairs=%llu "
                "sample_changes=%llu sample_duplicates=%llu sample_span_ms=%.3f sampled_change_hz=%.3f",
                static_cast<unsigned long long>(samples.accepted), static_cast<unsigned long long>(samples.sampled),
                static_cast<unsigned long long>(samples.compared), static_cast<unsigned long long>(samples.changed),
                static_cast<unsigned long long>(samples.same), elapsed_ms,
                elapsed_ms > 0 ? 1000.0 * samples.changed / elapsed_ms : 0.0);
        }
        if (result == VK_SUCCESS) release_frame_samples(device);
        else frame_samples = {}; // Queue completion is unknown. Keep its Vulkan allocation with the poisoned device.
#endif
        presentation_windows.Stop();
        const uint64_t gs_tick_hz = Threading::GetThreadTicksPerSecond();
        uint64_t cpu_ticks = 0, cpu_sample_span = 0;
        size_t sampled_intervals = 0;
        for (size_t i = 0; i < presentation_windows.Count(); ++i) {
            const auto& window = presentation_windows.Get(i);
            const auto [p50, p95, p99] = window.Percentiles();
            const bool gs_cpu_available = gs_tick_hz && window.first != 0 && window.last >= window.first;
            const uint64_t window_gs_cpu_ticks = gs_cpu_available ? window.last - window.first : 0;
            if (gs_cpu_available) {
                cpu_ticks += window_gs_cpu_ticks;
                cpu_sample_span += window.elapsed;
                sampled_intervals += presentation_windows.Intervals;
            }
            Console.WriteLn(
                "IZPS2_PRESENT_WINDOW start=%u accepted_intervals=%zu wall_start_tick=%llu wall_ms=%.3f "
                "present_p50_ms=%.3f present_p95_ms=%.3f present_p99_ms=%.3f present_max_ms=%.3f "
                "gs_cpu_ms=%.3f gs_cpu_available=%d",
                window.first_frame, presentation_windows.Intervals,
                static_cast<unsigned long long>(window.first_tick),
                Common::Timer::ConvertValueToMilliseconds(window.elapsed),
                Common::Timer::ConvertValueToMilliseconds(p50),
                Common::Timer::ConvertValueToMilliseconds(p95),
                Common::Timer::ConvertValueToMilliseconds(p99),
                Common::Timer::ConvertValueToMilliseconds(window.MaxInterval()),
                gs_cpu_available ? 1000.0 * window_gs_cpu_ticks / gs_tick_hz : 0.0,
                gs_cpu_available ? 1 : 0);
        }
        Console.WriteLn(
            "IZPS2_VIDEO_STATS mode=gpu_copy frames=%llu cpu_upload_bytes=0 acquire_ms=%.3f "
            "acquire_timeouts=%llu fence_waits=%llu fence_wait_ms=%.3f retained=%d "
            "gs_cpu_ticks=%llu gs_tick_hz=%llu frame_span_ms=%.3f "
            "gs_cpu_sampled_intervals=%zu gs_cpu_sample_span_ms=%.3f",
            static_cast<unsigned long long>(vulkan_output.frames),
            Common::Timer::ConvertValueToMilliseconds(vulkan_output.acquire_ticks),
            static_cast<unsigned long long>(vulkan_output.acquire_timeouts),
            static_cast<unsigned long long>(vulkan_output.fence_waits),
            Common::Timer::ConvertValueToMilliseconds(vulkan_output.fence_wait_ticks),
            result != VK_SUCCESS || vulkan_output.unconsumed_acquire,
            static_cast<unsigned long long>(cpu_ticks),
            static_cast<unsigned long long>(gs_tick_hz),
            Common::Timer::ConvertValueToMilliseconds(vulkan_output.last_frame_wall - vulkan_output.first_frame_wall),
            sampled_intervals, Common::Timer::ConvertValueToMilliseconds(cpu_sample_span));
#ifdef IZPS2_SWITCH2_BOUNDED_PRESENT
        Console.WriteLn("IZPS2_PRESENT_POLICY bounded=%d acquire_budget_ns=%llu busy_slot_deferrals=%llu",
            vulkan_output.bounded_acquire ? 1 : 0,
            static_cast<unsigned long long>(vulkan_output.bounded_acquire ? UINT64_C(2000000) : UINT64_C(100000000)),
            static_cast<unsigned long long>(vulkan_output.busy_slot_deferrals));
#endif
#ifdef IZPS2_SWITCH2_PRESENT_ISOLATION
        Console.WriteLn("IZPS2_PRESENT_ISOLATION enabled=%d interval=%u skipped=%llu accepted=%llu "
            "draw_submit_ms=%.3f queue_present_ms=%.3f",
            vulkan_output.sparse_present ? 1 : 0, isolation_interval,
            static_cast<unsigned long long>(vulkan_output.isolation_skips),
            static_cast<unsigned long long>(vulkan_output.frames),
            Common::Timer::ConvertValueToMilliseconds(vulkan_output.draw_submit_ticks),
            Common::Timer::ConvertValueToMilliseconds(vulkan_output.queue_present_ticks));
#endif
        vulkan_output = {};
    }
#endif
}

void iz_video_set_fps_limit(unsigned rate) {
    requested_frame_rate.store(rate, std::memory_order_relaxed);
}

void iz_video_shutdown() {
    // MTGS must already be joined before releasing CPU scanout/history storage.
    iz_video_finish_presentations();
#ifdef ENABLE_VULKAN
#ifdef __SWITCH2__
    gs_affinity_configured = false;
#endif
#endif
    ps2_sw_temporal_release();
    std::free(output); std::free(scratch); output = nullptr; scratch = nullptr;
    sb4_output = nullptr;
    sb4_input_focused = nullptr;
    nro_present = nullptr;
    nro_input_focused = nullptr;
    history = {};
    applied_frame_rate = 0;
    cadence.Configure(0, 0);
    iz_video_set_fps_limit(0);
}
void iz_video_present_field(unsigned field) {
    if (failed || !scratch || !output) return;
    const bool in_focus = sb4_output ? sb4_input_focused() != 0 : nro_input_focused();
    if (!in_focus) {
        cadence.Reset();
#ifdef ENABLE_VULKAN
        presentation_windows.Pause();
#endif
        return;
    }
#ifdef ENABLE_VULKAN
    if (GSGetCurrentRenderer() == GSRendererType::VK) {
        if (!sb4_output || !GSHeadless::IsEnabled() || !g_gs_device) return;

        if (vulkan_output.buffer) {
            present_vulkan_frame();
            return;
        }
        GSDeviceVK::BorrowedFrame frame{};
        const auto status = GSDeviceVK::GetInstance()->PollBorrowedFrame(frame);
        if (status == GSDeviceVK::BorrowedFrameResult::Pending) return;
        if (status == GSDeviceVK::BorrowedFrameResult::Failed) {
            fail("PS2 Vulkan frame readback failed.");
            return;
        }
        if (!frame_due()) return;
        const int result = ps2_native_video_present(sb4_output, frame.pixels, frame.bytes,
            frame.width, frame.height, frame.pitch, static_cast<uint32_t>(frame.serial));
        if (result) fail("PS2 Vulkan framebuffer submission failed.");
        else frame_accepted();
        return;
    }
#endif
    const auto* renderer = isa_native::GSRendererSW::GetInstance();
    if (!renderer->PCRTCDisplays.PCRTCDisplays[0].enabled && !renderer->PCRTCDisplays.PCRTCDisplays[1].enabled) {
        if (!frame_due()) return;
        std::memset(output, 0, width * height * 4);
        for (size_t i = 3; i < width * height * 4; i += 4) output[i] = 255;
        const int rc = present(0);
        if (rc && rc != EAGAIN) fail("PS2 framebuffer submission failed.");
        return;
    }
    Ps2SwFrameModeInfo raw{};
    const int rc = ps2_sw_frame_mode_compose_field(&history, scratch, scratch_bytes, field, &raw);
    if (rc == PS2_FRAME_MODE_HISTORY) return;
    if (rc) {
        char error[256];
        std::snprintf(error, sizeof(error), "GS scanout failed: %d (%s), field %u, GS frame %u", rc,
            raw.reason ? raw.reason : "unknown", field, raw.frame);
        fail(error); return;
    }
    if (!frame_due()) return;
    const Ps2SwMergeImage src{static_cast<const uint8_t*>(raw.rgba), raw.rgba_bytes, raw.pitch, raw.width, raw.height};
    const Ps2SwMergeOutput dst{output, width * height * 4, width * 4, width, height};
    Ps2SwPresentationOptions options{};
    options.desired_aspect_num = 4; options.desired_aspect_den = 3;
    options.viewport_aspect_num = 16; options.viewport_aspect_den = 9;
    if (ps2_sw_presentation_rgba(&src, &dst, &options, nullptr)) {
        fail("Cannot compose the PS2 picture for the native display."); return;
    }
    const int result = present(raw.frame);
    if (result && result != EAGAIN) { fail("PS2 framebuffer submission failed."); return; }
    if (!result && ++frames == 1)
        std::fprintf(stderr, "IZPS2_FIRST_GS_FRAME gs=%u source=%ux%u output=%ux%u\n", raw.frame, raw.width, raw.height, width, height);
}

uint64_t iz_video_accepted_presentations() {
    return accepted_presentations.load(std::memory_order_relaxed);
}
