// SPDX-License-Identifier: GPL-3.0+
#pragma once

#include <atomic>

namespace GSBackClose
{
enum class MtgsPhase : unsigned
{
    Requested,
    LeavingMainLoop,
    StoppingRenderer,
    DestroyingFront,
    DestroyingTextures,
    DestroyingRenderer,
    DestroyingDevice,
    ReleasingWindow,
    Closed,
};

enum class WorkerPhase : unsigned { Idle, StopPublished, Draining, Unregistering, Returned, Joined };

inline std::atomic<bool> Active{false};
inline std::atomic<MtgsPhase> Mtgs{MtgsPhase::Requested};
inline std::atomic<WorkerPhase> Worker{WorkerPhase::Idle};
}
