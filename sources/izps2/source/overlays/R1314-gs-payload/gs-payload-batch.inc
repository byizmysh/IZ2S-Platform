// SPDX-License-Identifier: GPL-3.0+
#include "gs-payload-batch.h"

namespace GsPayloadBatch
{
namespace
{
// In the hosted VM only the front GS on MTGS can publish transfer records.
// This state has the same owner as the existing front parser's staging node.
bool s_enabled = false;
const GSState* s_front = nullptr;
Slab<GSBackQueue::PayloadNode> s_slab;
uint64_t s_packets = 0;
uint64_t s_bytes = 0;
uint64_t s_acquires = 0;
}

void Configure(bool enabled)
{
    s_enabled = enabled;
    s_packets = 0;
    s_bytes = 0;
    s_acquires = 0;
}

std::string Report()
{
    return fmt::format("IZPS2_GS_PAYLOAD_BATCH enabled={} packets={} bytes={} acquired={}\n",
        unsigned(s_enabled), s_packets, s_bytes, s_acquires);
}
}
