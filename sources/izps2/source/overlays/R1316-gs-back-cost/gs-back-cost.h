// SPDX-License-Identifier: GPL-3.0+
#pragma once

#include <cstdint>

namespace GsBackCost
{
// Only at the completed VSync, after the front drained the producer queue.
void AtVsync(uint32_t field);
}
