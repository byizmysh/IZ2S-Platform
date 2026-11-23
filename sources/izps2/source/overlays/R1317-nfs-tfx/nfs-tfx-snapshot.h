// SPDX-License-Identifier: GPL-3.0+
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <utility>
#include <vector>

namespace NfsTfx
{
using Key = std::array<uint8_t, 32>;
using Fingerprint = std::array<uint8_t, 16>;
constexpr size_t MaxKeys = 128;
constexpr size_t MaxPayload = Fingerprint{}.size() + MaxKeys * Key{}.size();

struct Snapshot
{
    Fingerprint fingerprint{};
    std::vector<Key> keys;

    bool Load(std::span<const uint8_t> data)
    {
        if (data.size() <= fingerprint.size() || data.size() > MaxPayload ||
            (data.size() - fingerprint.size()) % sizeof(Key) != 0)
            return false;
        Snapshot decoded;
        std::memcpy(decoded.fingerprint.data(), data.data(), decoded.fingerprint.size());
        for (size_t offset = fingerprint.size(); offset < data.size(); offset += sizeof(Key))
        {
            Key key;
            std::memcpy(key.data(), data.data() + offset, sizeof(key));
            if (!decoded.Insert(key))
                return false;
        }
        *this = std::move(decoded);
        return true;
    }

    bool Insert(const Key& key)
    {
        if (keys.size() >= MaxKeys || std::find(keys.begin(), keys.end(), key) != keys.end())
            return false;
        keys.push_back(key);
        return true;
    }

    std::vector<uint8_t> Save() const
    {
        if (keys.empty())
            return {};
        std::vector<uint8_t> data(fingerprint.size() + keys.size() * sizeof(Key));
        std::memcpy(data.data(), fingerprint.data(), fingerprint.size());
        std::memcpy(data.data() + fingerprint.size(), keys.data(), keys.size() * sizeof(Key));
        return data;
    }
};

void Reset();
void Arm(const uint32_t* stop_requested);
void ReportAndSave(bool allow_write);
}
