#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>

struct EeByteCopyGprs
{
    unsigned source, destination, counter, stop, value;
};

// LBU value,0(source); ADDIU counter,-1; ADDIU source,1; SB value,0(destination);
// ADDIU destination,1; BNE counter,stop,loop; NOP.
inline std::optional<EeByteCopyGprs> DecodeEeByteCopy(const std::array<std::uint32_t, 7>& code)
{
    const auto rs = [](auto word) { return (word >> 21) & 31; };
    const auto rt = [](auto word) { return (word >> 16) & 31; };
    const auto inc = [](unsigned reg, unsigned delta) { return 0x24000000u | reg << 21 | reg << 16 | delta; };
    const EeByteCopyGprs gprs{rs(code[0]), rs(code[3]), rs(code[1]), rt(code[5]), rt(code[0])};
    if (code[0] != (0x90000000u | gprs.source << 21 | gprs.value << 16) ||
        code[1] != inc(gprs.counter, 0xffff) || code[2] != inc(gprs.source, 1) ||
        code[3] != (0xa0000000u | gprs.destination << 21 | gprs.value << 16) ||
        code[4] != inc(gprs.destination, 1) ||
        code[5] != (0x1400fffau | gprs.counter << 21 | gprs.stop << 16) || code[6] != 0)
        return std::nullopt;

    std::array roles{gprs.source, gprs.destination, gprs.counter, gprs.stop, gprs.value};
    std::sort(roles.begin(), roles.end());
    if (!roles.front() || std::adjacent_find(roles.begin(), roles.end()) != roles.end())
        return std::nullopt;
    return gprs;
}
