// SPDX-License-Identifier: GPL-3.0+
#pragma once

#include <cstdint>
#include <string_view>

namespace GtaVuCache
{
enum class Policy : std::uint8_t
{
    Unmanaged,
    Disabled,
    KnownGame,
    Forced,
};

// The VM owner sets this before CPU initialization and keeps it until all
// VM and CPU providers have stopped.
inline Policy SessionPolicy = Policy::Unmanaged;

constexpr bool Enabled(Policy policy, std::string_view serial, std::uint32_t disc_crc)
{
    if (policy == Policy::Forced)
        return true;
    if (policy != Policy::KnownGame)
        return false;
    return (serial == "SLES-52541" && disc_crc == 0xb440a8fe) ||
           (serial == "SLUS-20946" && disc_crc == 0x399a49ca);
}

template <typename RecompilerOptions>
void Apply(RecompilerOptions& recompiler, std::string_view serial, std::uint32_t disc_crc)
{
    if (SessionPolicy != Policy::Unmanaged)
        recompiler.EnableVUProgramCache = recompiler.EnableVU1 &&
            Enabled(SessionPolicy, serial, disc_crc);
}
}
