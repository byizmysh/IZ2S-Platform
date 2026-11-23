// SPDX-License-Identifier: GPL-3.0+
#pragma once

#include <atomic>

struct Ps2Sb4Worker;

namespace GsBackNative
{
enum class Phase : unsigned
{
    Idle, WaitingForHandle, WaitingForExit, Closing, Released,
    CreateUnknown, HandleMissing, HandleMismatch, ExitUnconfirmed, CloseRejected,
};
inline std::atomic<Phase> Progress{Phase::Idle};
void Configure(Ps2Sb4Worker* worker);
}
