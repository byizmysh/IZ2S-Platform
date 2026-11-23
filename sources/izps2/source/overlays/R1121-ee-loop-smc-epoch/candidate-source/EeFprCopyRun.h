#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

struct EeFprCopySpan
{
    unsigned base = 0;
    int low = 0;
    int end = 0;
    int delta = 0;
    bool updated = false;

    constexpr bool addWord(int offset)
    {
        const int start = end == low ? offset : std::min(low, offset);
        const int nextEnd = end == low ? offset + 4 : std::max(end, offset + 4);
        if ((end != low && (offset - low) % 4) || nextEnd - start > 256)
            return false;
        low = start;
        end = nextEnd;
        return true;
    }
};

struct EeFprCopyRun
{
    static constexpr unsigned MaxWords = 20;
    struct Copy { int source, destination; };

    EeFprCopySpan source, destination;
    std::array<Copy, MaxWords / 2> copies{};
    unsigned count = 0;
    unsigned words = 0;
    unsigned fpr = 0;
};

constexpr EeFprCopyRun eeDecodeFprCopyRun(const std::uint32_t* ops, unsigned size)
{
    EeFprCopyRun plan, complete;
    if (size < 6 || (ops[0] >> 26) != 0x31)
        return {};
    plan.source.base = (ops[0] >> 21) & 31;
    plan.fpr = (ops[0] >> 16) & 31;

    bool pending = false;
    int sourceOffset = 0;
    for (unsigned i = 0; i < size && i < EeFprCopyRun::MaxWords; ++i)
    {
        const auto op = ops[i];
        const unsigned base = (op >> 21) & 31;
        const unsigned reg = (op >> 16) & 31;
        const int offset = static_cast<std::int16_t>(op);
        if ((op >> 26) == 0x09 && base != 0 && base == reg && plan.count)
        {
            if (base != plan.source.base && base != plan.destination.base)
                break;
            if (base == plan.source.base)
            {
                plan.source.delta += offset;
                plan.source.updated = true;
            }
            if (base == plan.destination.base)
            {
                plan.destination.delta += offset;
                plan.destination.updated = true;
            }
            continue;
        }
        if (reg != plan.fpr)
            break;
        if (!pending && (op >> 26) == 0x31 && base == plan.source.base)
        {
            sourceOffset = offset + plan.source.delta;
            if (!plan.source.addWord(sourceOffset))
                break;
            pending = true;
            continue;
        }
        if (!pending || (op >> 26) != 0x39)
            break;
        if (!plan.count)
            plan.destination.base = base;
        if (base != plan.destination.base)
            break;
        const int destinationOffset = offset + plan.destination.delta;
        if (!plan.destination.addWord(destinationOffset))
            break;
        plan.copies[plan.count++] = {sourceOffset, destinationOffset};
        plan.words = i + 1;
        complete = plan;
        pending = false;
    }
    return complete.count >= 3 ? complete : EeFprCopyRun{};
}
