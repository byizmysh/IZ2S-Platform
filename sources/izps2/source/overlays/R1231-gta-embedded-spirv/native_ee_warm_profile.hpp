#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace ps2::ee_warm {

constexpr unsigned HeaderBytes = 24;
constexpr unsigned RecordBytes = 16;
#ifdef IZPS2_SWITCH2_EE_WARM
constexpr unsigned DefaultEntries = 4096;
constexpr unsigned MaxEntries = 16384;
#else
constexpr unsigned MaxEntries = 1024;
constexpr unsigned DefaultEntries = MaxEntries;
#endif
constexpr unsigned CheckpointMaxEntries = 20480;
constexpr unsigned MaxBytes = HeaderBytes + RecordBytes * MaxEntries;
constexpr unsigned BatchSize = 16;
constexpr std::array<uint8_t, 8> Magic{'I','Z','2','S','E','E','W','2'};

size_t ReadSdProfile(const std::string& key, std::span<uint8_t> output);
bool WriteSdProfile(const std::string& key, std::span<const uint8_t> input);

inline uint64_t Hash(std::span<const uint8_t> bytes)
{
    uint64_t hash = 14695981039346656037ull;
    for (const uint8_t byte : bytes)
        hash = (hash ^ byte) * 1099511628211ull;
    return hash;
}

inline uint64_t ReadLE(std::span<const uint8_t> bytes)
{
    uint64_t value = 0;
    for (size_t i = 0; i < bytes.size(); ++i)
        value |= uint64_t{bytes[i]} << (8 * i);
    return value;
}

inline void WriteLE(std::span<uint8_t> bytes, uint64_t value)
{
    for (auto& byte : bytes)
    {
        byte = static_cast<uint8_t>(value);
        value >>= 8;
    }
}

inline std::string Key(uint32_t disc, uint32_t elf)
{
    return "ee-warm2-" + std::to_string(disc) + '-' + std::to_string(elf);
}

struct Result {
    unsigned usable = 0;
    unsigned stale = 0;
    bool valid = false;
};

enum class Order { Independent, Prefix, EmbeddedPrefix };

struct Entry {
    uint32_t pc;
    uint32_t bytes;
};

// The caller holds the EE and IOP at a safe point for the lifetime of ram.
// submit returns false to stop after an interrupted or refused batch.
template<class Submit>
Result Warm(std::span<const uint8_t> profile, uint32_t disc, uint32_t elf,
            std::span<const uint8_t> ram, Submit submit, unsigned limit = DefaultEntries,
            Order order = Order::Independent)
{
    Result result;
    const unsigned capacity = order == Order::EmbeddedPrefix ? CheckpointMaxEntries : MaxEntries;
    if (!disc || !elf || profile.size() < HeaderBytes || profile.size() > HeaderBytes + RecordBytes * capacity ||
        !std::equal(Magic.begin(), Magic.end(), profile.begin()) ||
        ReadLE(profile.subspan(8, 4)) != disc || ReadLE(profile.subspan(12, 4)) != elf)
        return result;
    const auto count = ReadLE(profile.subspan(16, 4));
    const auto body = profile.subspan(HeaderBytes);
    if (!count || count > std::min(limit, capacity) || body.size() != count * RecordBytes ||
        ReadLE(profile.subspan(20, 4)) != 0)
        return result;

    // Validate all record geometry before any session can compile a batch.
    for (size_t pos = 0; pos < body.size(); pos += RecordBytes)
    {
        const auto pc = ReadLE(body.subspan(pos, 4));
        const auto size = ReadLE(body.subspan(pos + 4, 4));
        if (pc % 4 || !size || size % 4 || size > 4096 - (pc % 4096) ||
            pc >= ram.size() || size > ram.size() - pc)
            return result;
    }

    result.valid = true;
    std::array<Entry, BatchSize> entries{};
    unsigned num = 0;
    for (size_t pos = 0; pos < body.size(); pos += RecordBytes)
    {
        const auto pc = ReadLE(body.subspan(pos, 4));
        const auto size = ReadLE(body.subspan(pos + 4, 4));
        if (Hash(ram.subspan(pc, size)) != ReadLE(body.subspan(pos + 8, 8)))
        {
            ++result.stale;
            if (order != Order::Independent)
                return result;
            continue;
        }
        ++result.usable;
        entries[num++] = {static_cast<uint32_t>(pc), static_cast<uint32_t>(size)};
        if (num == entries.size())
        {
            if (!submit(std::span(entries)))
                return result;
            num = 0;
        }
    }
    if (num)
        submit(std::span(entries).first(num));
    return result;
}

} // namespace ps2::ee_warm
