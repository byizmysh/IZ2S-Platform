#pragma once

#include <cstdint>

struct NativeEeCompileTiming
{
    std::uint64_t blocks, total_ticks, commit_ticks, regressions;

    void Complete(std::uint64_t begin, std::uint64_t before_commit, std::uint64_t end)
    {
        if (before_commit < begin || end < before_commit)
        {
            ++regressions;
            return;
        }
        ++blocks;
        total_ticks += end - begin;
        commit_ticks += end - before_commit;
    }
};
static_assert(sizeof(NativeEeCompileTiming) == 32);

struct NativeEeCompileBatch
{
    std::uint64_t valid, blocks, total_ticks, commit_ticks;

    static NativeEeCompileBatch Compare(const NativeEeCompileTiming& before, const NativeEeCompileTiming& after)
    {
        if (before.regressions != after.regressions || before.blocks > after.blocks ||
            before.total_ticks > after.total_ticks || before.commit_ticks > after.commit_ticks)
            return {};
        const auto elapsed = after.total_ticks - before.total_ticks;
        const auto commit = after.commit_ticks - before.commit_ticks;
        if (commit > elapsed)
            return {};
        return {1, after.blocks - before.blocks, elapsed, commit};
    }
};
static_assert(sizeof(NativeEeCompileBatch) == 32);

extern "C" NativeEeCompileTiming ps2_native_ee_compile_timing;
