#include "ps2_sb4_session.h"
#include <sb4/host-posix.h>
#include <sb4/host-runtime.h>

extern Sb4HostHook __preinit_array_start[], __preinit_array_end[];
extern Sb4HostHook __init_array_start[], __init_array_end[];
extern Sb4HostHook __fini_array_start[], __fini_array_end[];

static uint32_t initialize_runtime(IzNativeApp *app) {
    const Sb4Compat *compat = sb4ModuleCompat(app);
    const uint32_t posix = sb4HostPosixBind(compat);
    if (posix) return posix;
    const Sb4HostHooks hooks = {
        .preinit = __preinit_array_start,
        .init = __init_array_start,
        .fini = __fini_array_start,
        .preinit_count = __preinit_array_end - __preinit_array_start,
        .init_count = __init_array_end - __init_array_start,
        .fini_count = __fini_array_end - __fini_array_start,
    };
    const uint32_t result = sb4HostRuntimeInitialize(compat, &hooks);
    return result == SB4_HOST_RUNTIME_RETAINED ? (uint32_t)PS2_SB4_SESSION_RETAINED : result;
}

uint64_t ps2_sb4_hosted_entry(void *context, uint64_t reserved) {
    static const Ps2Sb4EngineRuntime runtime = {
        .initialize = initialize_runtime,
        .quiesce = sb4HostPosixQuiesce,
        .finalize = sb4HostRuntimeFinalize,
        .can_read_storage_from_threads = sb4HostPosixCanReadStorageFromThreads,
    };
    return ps2_sb4_engine_entry_with_runtime(context, reserved, &runtime);
}
