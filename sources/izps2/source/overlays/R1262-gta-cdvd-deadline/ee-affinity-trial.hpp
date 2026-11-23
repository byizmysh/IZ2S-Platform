#pragma once

#include <bit>
#include <cstdint>
#include <limits>

struct EeThreadAffinity
{
    std::int32_t preferred = -1;
    std::uint64_t mask = 0;
};

// Exclude a VU core only when the kernel reports a single pinned core.
// Retain all other processors the EE owner was already permitted to use.
inline bool SelectEeAffinity(EeThreadAffinity ee, EeThreadAffinity vu, EeThreadAffinity& selected)
{
    if (ee.preferred < -1 || ee.preferred >= 32 ||
        ee.mask > std::numeric_limits<std::uint32_t>::max() ||
        !std::has_single_bit(vu.mask) || !(ee.mask & vu.mask))
        return false;
    const std::uint64_t remaining = ee.mask & ~vu.mask;
    if (!remaining)
        return false;
    selected = {ee.preferred, remaining};
    if (selected.preferred >= 0 && !(remaining & (std::uint64_t{1} << selected.preferred)))
        selected.preferred = static_cast<std::int32_t>(std::countr_zero(remaining));
    return true;
}

template <typename Platform>
class EeAffinityTrial
{
public:
    EeAffinityTrial() = default;
    EeAffinityTrial(const EeAffinityTrial&) = delete;
    EeAffinityTrial& operator=(const EeAffinityTrial&) = delete;
    ~EeAffinityTrial() { Restore(); }

    // The worker is lent by the existing MTVU owner. Query only here,
    // while the VM and the known worker are still live.
    void Start(bool restrict_mask, std::uint32_t vu_handle)
    {
        if (!vu_handle || started)
            return;
        started = true;
        ee_query = Platform::Get(Platform::Current, saved);
        vu_query = Platform::Get(vu_handle, vu);
        if (ee_query || vu_query || !restrict_mask || !SelectEeAffinity(saved, vu, selected))
            return;
        set_result = Platform::Set(Platform::Current, selected);
        if (!set_result)
        {
            changed = true;
            EeThreadAffinity actual;
            verify_result = Platform::Get(Platform::Current, actual);
            verified = !verify_result && actual.mask == selected.mask && !(actual.mask & vu.mask);
            if (!verified)
                Restore();
        }
    }

    void Restore()
    {
        if (!changed)
            return;
        restore_attempted = true;
        restore_result = Platform::Set(Platform::Current, saved);
        if (!restore_result)
            changed = false;
    }

    EeThreadAffinity saved{}, vu{}, selected{};
    std::uint32_t ee_query = 0, vu_query = 0, set_result = 0, verify_result = 0, restore_result = 0;
    bool started = false, verified = false, changed = false, restore_attempted = false;
};
