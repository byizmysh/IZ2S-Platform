// SPDX-License-Identifier: GPL-3.0+
#pragma once

#include <atomic>

namespace GSBackClose
{
enum class MtgsPhase : unsigned
{
    Requested,
    StoppingCapture,
    StoppingRenderer,
    DestroyingFront,
    DestroyingTextures,
    DestroyingRenderer,
    DestroyingImGui,
    DestroyingDevice,
    ReleasingWindow,
    Closed,
};

enum class WorkerPhase : unsigned { Idle, StopPublished, Draining, Unregistering, Returned, Joined };

enum class VulkanPhase : unsigned
{
    None,
    InitialIdle,
    DestroyingBackbuffers,
    AcquiringInstanceLock,
    DestroyingBaseResources,
    SubmittingFinalCommands,
    FinalIdle,
    DestroyingVulkanResources,
    ReleasingLoader,
};

inline std::atomic<bool> Active{false};
inline std::atomic<VulkanPhase> Vulkan{VulkanPhase::None};
inline std::atomic<MtgsPhase> Mtgs{MtgsPhase::Requested};
inline std::atomic<WorkerPhase> Worker{WorkerPhase::Idle};
}
