#pragma once

#include "native_ee_warm_profile.hpp"

#include <bit>
#include <vector>

namespace ps2::ee_warm {

// Both inputs have already passed validation. Keep the previous first-use
// order, reserving the original 1024-entry budget for matching startup blocks.
// Under pressure, let longer demand-compiled blocks replace shorter matches
// beyond that prefix. Equal lengths retain their previous priority.
inline bool MergeProfiles(std::span<const uint8_t> previous, std::span<const uint32_t> matched,
                          std::span<const uint8_t> observed, std::vector<uint8_t>& output,
                          unsigned limit = DefaultEntries)
{
    if (observed.empty())
        return false;
    if (previous.empty())
    {
        output.assign(observed.begin(), observed.end());
        return true;
    }

    using Record = std::span<const uint8_t, RecordBytes>;
    struct Observation {
        uint32_t pc;
        Record record;
    };
    struct Candidate {
        Record record;
        uint32_t guest_bytes;
        size_t order;
    };
    std::vector<Observation> updates;
    updates.reserve((observed.size() - HeaderBytes) / RecordBytes);
    for (size_t at = HeaderBytes; at < observed.size(); at += RecordBytes)
    {
        const Record record(observed.data() + at, RecordBytes);
        updates.push_back({static_cast<uint32_t>(ReadLE(record.first<4>())), record});
    }
    std::sort(updates.begin(), updates.end(), [](const Observation& a, const Observation& b) {
        return a.pc < b.pc;
    });

    const size_t total = (previous.size() + observed.size() - 2 * HeaderBytes) / RecordBytes;
    // Word-aligned EE PCs leave UINT32_MAX free as an empty slot.
    constexpr uint32_t unused = UINT32_MAX;
    const auto slots = std::bit_ceil(std::max<size_t>(total * 2, 2));
    std::vector<uint32_t> included(slots, unused);
    const auto mask = static_cast<uint32_t>(slots - 1);
    const auto shift = 32 - std::bit_width(mask);
    std::vector<Candidate> candidates;
    candidates.reserve(total);
    const auto append = [&](uint32_t pc, Record record) {
        uint32_t slot = ((pc >> 2) * 2654435761u) >> shift;
        while (included[slot] != unused && included[slot] != pc)
            slot = (slot + 1) & mask;
        if (included[slot] == pc)
            return;
        included[slot] = pc;
        candidates.push_back({record, static_cast<uint32_t>(ReadLE(record.subspan<4, 4>())), candidates.size()});
    };
    const auto append_previous = [&](bool was_present) {
        for (size_t at = HeaderBytes; at < previous.size(); at += RecordBytes)
        {
            if (!was_present && candidates.size() == limit)
                break;
            const Record record(previous.data() + at, RecordBytes);
            const auto pc = static_cast<uint32_t>(ReadLE(record.first<4>()));
            if (was_present != std::binary_search(matched.begin(), matched.end(), pc))
                continue;
            const auto update = std::lower_bound(updates.begin(), updates.end(), pc,
                [](const Observation& candidate, uint32_t address) { return candidate.pc < address; });
            append(pc, update != updates.end() && update->pc == pc ? update->record : record);
        }
    };
    append_previous(true);
    constexpr size_t StartupPrefixEntries = 1024;
    const auto protected_count = std::min<size_t>(limit > DefaultEntries ? limit : StartupPrefixEntries,
        candidates.size());
    for (size_t at = HeaderBytes; at < observed.size(); at += RecordBytes)
    {
        const Record record(observed.data() + at, RecordBytes);
        append(static_cast<uint32_t>(ReadLE(record.first<4>())), record);
    }
    if (candidates.size() > limit)
    {
        const auto tail = candidates.begin() + protected_count;
        const auto end = candidates.begin() + limit;
        std::nth_element(tail, end, candidates.end(), [](const Candidate& a, const Candidate& b) {
            if (a.guest_bytes != b.guest_bytes)
                return a.guest_bytes > b.guest_bytes;
            return a.order < b.order;
        });
        std::sort(tail, end, [](const Candidate& a, const Candidate& b) { return a.order < b.order; });
        candidates.erase(end, candidates.end());
    }
    else if (candidates.size() < limit)
    {
        append_previous(false);
    }

    output.clear();
    output.reserve(HeaderBytes + RecordBytes * candidates.size());
    output.insert(output.end(), observed.begin(), observed.begin() + HeaderBytes);
    for (const Candidate& candidate : candidates)
        output.insert(output.end(), candidate.record.begin(), candidate.record.end());
    WriteLE(std::span(output).subspan(16, 4), candidates.size());
    return !std::equal(output.begin(), output.end(), previous.begin(), previous.end());
}

} // namespace ps2::ee_warm
