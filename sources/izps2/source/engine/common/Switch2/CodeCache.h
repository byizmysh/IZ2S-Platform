#pragma once

#include <cstddef>
#include <cstdint>

struct Sb4JitCacheApi;

namespace HostSys
{
void SetJitCacheApi(const Sb4JitCacheApi* api);
void* AllocateJitCache(size_t size);
void ReleaseJitCache();
void* GetWritableCodeAddress(void* address);
#if defined(IZPS2_WRAPPER_AUDIT) || defined(IZPS2_SWITCH2_SESSION_TIMING)
struct JitCacheTiming
{
    uint64_t calls;
    uint64_t bytes;
    uint64_t ticks;
    uint64_t max_ticks;
};
JitCacheTiming GetJitCacheTiming();
#endif
}
