#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

// The EE compiler owns the cache and the constant pool for the same generation.
class NativeEeSmcSnapshots
{
public:
    struct Entry
    {
        const std::uint8_t* bytes;
        std::size_t size;
    };

    Entry& Slot(const std::uint8_t* live, std::size_t size)
    {
        std::uint32_t first, last;
        std::memcpy(&first, live, sizeof(first));
        std::memcpy(&last, live + size - sizeof(last), sizeof(last));
        const std::uint32_t hash = (first * 0x9e3779b1u) ^ (last * 0x85ebca6bu) ^ static_cast<std::uint32_t>(size);
        return m_entries[(hash ^ (hash >> 16)) & (kEntries - 1)];
    }

    static bool Matches(const Entry& entry, const std::uint8_t* live, std::size_t size)
    {
        return entry.bytes && entry.size == size && std::memcmp(entry.bytes, live, size) == 0;
    }

    void Reset() { std::memset(m_entries, 0, sizeof(m_entries)); }

private:
    static constexpr std::size_t kEntries = 256;
    Entry m_entries[kEntries];
};
