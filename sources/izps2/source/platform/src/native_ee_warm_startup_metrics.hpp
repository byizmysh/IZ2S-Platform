#pragma once

#include <cstddef>
#include <cstdint>

namespace ps2::ee_warm {

struct StartupMetrics {
    std::uint64_t status = 0;
    std::uint64_t bytes = 0;
    std::uint64_t matched = 0;
    std::uint64_t stale = 0;
    std::uint64_t admitted = 0;
    std::uint64_t batches = 0;
    std::uint64_t read_ticks = 0;
    std::uint64_t warm_ticks = 0;
    std::uint64_t save_status = 0;

    enum Status { Skipped, Unavailable, Rejected, Valid, Cancelled };
    enum SaveStatus { NotAttempted, NoUpdate, Saved, WriteFailed };

    static bool CanSave(bool cache_key_current, bool runtime_failed, bool guest_started)
    {
        return cache_key_current && !runtime_failed && guest_started;
    }

    void CountBatch(std::size_t consumed)
    {
        admitted += consumed;
        ++batches;
    }
};

static_assert(sizeof(StartupMetrics) == 9 * sizeof(std::uint64_t));

} // namespace ps2::ee_warm
