// SPDX-License-Identifier: GPL-3.0+
#pragma once

#include <cstddef>
#include <cstdint>

#include <sb4/jit-cache.h>

struct Ps2IopJitLeaseProbe
{
    Sb4JitCache cache{};
    std::uint32_t open_result = 0;
    std::uint32_t first_close_result = 0;
    std::uint32_t close_result = 0;
    bool started = false;
    bool qualified = false;

    // This call runs on a joined worker. The provider's open performs
    // its own write, initial execution and changed-range qualification.
    void Run(const Sb4JitCacheApi& api, std::size_t bytes)
    {
        started = true;
        open_result = api.open(&cache, bytes);
        qualified = open_result == 0 && cache.owner && cache.rw && cache.rx && cache.rw != cache.rx;
        if (!cache.owner)
            return;

        first_close_result = api.close(&cache);
        close_result = first_close_result;
        if (close_result && cache.owner)
            close_result = api.close(&cache);
    }

    bool Retained() const { return cache.owner != nullptr; }
};
