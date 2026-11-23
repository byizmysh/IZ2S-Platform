#include <switch/kernel/svc.h>

__attribute__((visibility("hidden")))
u32 iz_ps2_affinity_get(u32 handle, s32* preferred, u64* mask)
{
    return svcGetThreadCoreMask(preferred, mask, handle ? handle : CUR_THREAD_HANDLE);
}

__attribute__((visibility("hidden")))
u32 iz_ps2_affinity_set(u32 handle, s32 preferred, u32 mask)
{
    return svcSetThreadCoreMask(handle ? handle : CUR_THREAD_HANDLE, preferred, mask);
}
