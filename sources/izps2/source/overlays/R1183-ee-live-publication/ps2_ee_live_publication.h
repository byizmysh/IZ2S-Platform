#pragma once

#include "ps2_ee_warm_ranges.h"
#include <cstdint>
#include <cstring>

struct Ps2EeLivePublicationPlan
{
    static constexpr size_t PageBytes = 4096;
    static constexpr size_t MaxGapBytes = 256;
    static constexpr size_t TotalGapBudget = 1024;

    std::array<Ps2EeWarmRange, PS2_EE_WARM_MAX_RANGES> ranges{};
    size_t count = 0;
    size_t unchanged_bytes = 0;

    // The input is the sorted union of requested writes within the EE-owned
    // region. Finishing inside a page avoids adding a provider page operation.
    // Verify the intervening bytes in both views before widening any request.
    Ps2EeLivePublicationPlan(std::span<const Ps2EeWarmRange> exact,
        const std::uint8_t* rw, const std::uint8_t* rx)
    {
        for (const auto& range : exact)
        {
            if (count)
            {
                auto& previous = ranges[count - 1];
                const size_t end = previous.offset + previous.size;
                const size_t gap = range.offset - end;
                if (gap <= MaxGapBytes && gap <= TotalGapBudget - unchanged_bytes &&
                    (end - 1) / PageBytes == range.offset / PageBytes &&
                    std::memcmp(rw + end, rx + end, gap) == 0)
                {
                    previous.size = range.offset + range.size - previous.offset;
                    unchanged_bytes += gap;
                    continue;
                }
            }
            ranges[count++] = range;
        }
    }

    std::span<const Ps2EeWarmRange> Pending() const { return std::span(ranges).first(count); }
};

void iz_ps2_configure_live_ee_publication(bool enabled);
bool iz_ps2_live_ee_publication_enabled();
bool iz_ps2_begin_live_ee_publication();
void iz_ps2_log_live_ee_publication();
