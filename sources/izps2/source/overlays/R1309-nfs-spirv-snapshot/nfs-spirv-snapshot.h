// SPDX-License-Identifier: GPL-3.0+
#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <map>
#include <span>
#include <utility>
#include <vector>

namespace NfsSpirvSnapshot
{
using Key = std::array<uint8_t, 24>;
using Words = std::vector<uint32_t>;
constexpr size_t MaxPayload = 4 * 1024 * 1024;
constexpr size_t MaxRecord = 128 * 1024;
constexpr size_t MaxEntries = 512;
constexpr uint32_t SpirvMagic = 0x07230203;
constexpr size_t RecordHeader = sizeof(Key) + sizeof(uint32_t);

inline bool IsValid(const Key& key, const Words& words)
{
    uint32_t source_length = 0;
    uint32_t stage = 0;
    std::memcpy(&source_length, key.data() + 16, sizeof(source_length));
    std::memcpy(&stage, key.data() + 20, sizeof(stage));
    return source_length != 0 && stage <= 5 && words.size() >= 5 &&
        words.size() <= MaxRecord / sizeof(uint32_t) && words[0] == SpirvMagic &&
        words[3] != 0 && words[4] == 0;
}

class Snapshot
{
public:
    bool Insert(const Key& key, Words words)
    {
        if (!IsValid(key, words))
            return false;
        const size_t size = RecordHeader + words.size() * sizeof(uint32_t);
        if (m_modules.size() == MaxEntries || size > MaxPayload - m_bytes)
            return false;
        if (!m_modules.emplace(key, std::move(words)).second)
            return false;
        m_bytes += size;
        return true;
    }

    const Words* Find(const Key& key) const
    {
        const auto found = m_modules.find(key);
        return found == m_modules.end() ? nullptr : &found->second;
    }

    bool Load(std::span<const uint8_t> payload)
    {
        if (payload.size() > MaxPayload)
            return false;
        Snapshot decoded;
        while (!payload.empty())
        {
            if (payload.size() < RecordHeader || decoded.Size() == MaxEntries)
                return false;
            Key key;
            uint32_t bytes = 0;
            std::memcpy(key.data(), payload.data(), key.size());
            std::memcpy(&bytes, payload.data() + key.size(), sizeof(bytes));
            payload = payload.subspan(RecordHeader);
            if (bytes < 5 * sizeof(uint32_t) || bytes > MaxRecord ||
                bytes > payload.size() || bytes % sizeof(uint32_t))
                return false;
            Words words(bytes / sizeof(uint32_t));
            std::memcpy(words.data(), payload.data(), bytes);
            if (!decoded.Insert(key, std::move(words)))
                return false;
            payload = payload.subspan(bytes);
        }
        *this = std::move(decoded);
        return true;
    }

    std::vector<uint8_t> Serialize() const
    {
        std::vector<uint8_t> payload(m_bytes);
        size_t position = 0;
        for (const auto& [key, words] : m_modules)
        {
            const uint32_t bytes = static_cast<uint32_t>(words.size() * sizeof(uint32_t));
            std::memcpy(payload.data() + position, key.data(), key.size());
            std::memcpy(payload.data() + position + key.size(), &bytes, sizeof(bytes));
            position += RecordHeader;
            std::memcpy(payload.data() + position, words.data(), bytes);
            position += bytes;
        }
        return payload;
    }

    size_t Size() const { return m_modules.size(); }

private:
    std::map<Key, Words> m_modules;
    size_t m_bytes = 0;
};
}

namespace NfsSpirvCache
{
void Begin();
void ReportAndSave(bool allow_write);
void Reset();
}
