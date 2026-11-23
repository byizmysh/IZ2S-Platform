#pragma once

#include <cstddef>
#include <sb4/jit-cache.h>

struct Ps2JitWorkerScratch {
    const Sb4JitCache* arena = nullptr;
    std::size_t offset = 0;
};

// VM owner only. The reserved page lies outside every emulator code region.
void iz_reserve_jit_worker_scratch(bool enabled);
Ps2JitWorkerScratch iz_jit_worker_scratch();
void iz_retain_jit_worker_scratch();
