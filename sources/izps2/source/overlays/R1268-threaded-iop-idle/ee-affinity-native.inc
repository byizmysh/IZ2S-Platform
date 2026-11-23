#include "ee-affinity-trial.hpp"

extern "C" std::uint32_t iz_ps2_affinity_get(std::uint32_t, std::int32_t*, std::uint64_t*);
extern "C" std::uint32_t iz_ps2_affinity_set(std::uint32_t, std::int32_t, std::uint32_t);

namespace {
struct EeAffinityPlatform
{
    static constexpr std::uint32_t Current = 0;

    static std::uint32_t Get(std::uint32_t handle, EeThreadAffinity& out)
    {
        return iz_ps2_affinity_get(handle, &out.preferred, &out.mask);
    }

    static std::uint32_t Set(std::uint32_t handle, EeThreadAffinity chosen)
    {
        return iz_ps2_affinity_set(handle, chosen.preferred, static_cast<std::uint32_t>(chosen.mask));
    }
};
}
