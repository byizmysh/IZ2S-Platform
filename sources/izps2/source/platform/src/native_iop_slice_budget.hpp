#pragma once

#include "IopThread/IopThreadMode.h"

// The IOP coalesces small watermark updates only when nothing is due. Keep
// the threshold below even the minimum supported EE-to-IOP lag.
inline constexpr s32 ps2_native_iop_min_slice = IopThread::kEeWaitCycles / 2;
static_assert(ps2_native_iop_min_slice > 0 && ps2_native_iop_min_slice < IopThread::kMinLag);
