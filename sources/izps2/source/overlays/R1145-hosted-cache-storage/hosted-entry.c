#include "ps2_sb4_session.h"
#include <sb4/host-posix.h>
#include <sb4/host-runtime.h>

extern Sb4HostHook __preinit_array_start[], __preinit_array_end[];
extern Sb4HostHook __init_array_start[], __init_array_end[];
extern Sb4HostHook __fini_array_start[], __fini_array_end[];

static IzNativeApp *boot_app;

void izps2_boot_stage(IzNativeApp *app, const char *message, uint32_t length) {
    if (app) boot_app = app;
    if (!boot_app) return;
    app = boot_app;
    static const char name[] = "r1145-worker-start.log";
    const Sb4StorageFiles *files = sb4StorageFiles(sb4ModuleStorage(app));
    if (!files) return;
    uint32_t token = 0;
    uint64_t size = 0;
    if (files->open(app, name, SB4_STORAGE_OPEN_UPDATE, &token, &size)) {
        if (files->create(app, name, 0) ||
            files->open(app, name, SB4_STORAGE_OPEN_UPDATE, &token, &size)) return;
    }
    const uint32_t result = files->write(app, token, size, message, length);
    if (!result) files->flush(app, token);
    files->close(app, token);
    if (!result) files->commit(app);
}

#define WORKER_STAGE(app, message) izps2_boot_stage((app), (message), sizeof(message) - 1)

static uint32_t initialize_runtime(IzNativeApp *app) {
    WORKER_STAGE(app, "02 posix bind\n");
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
    WORKER_STAGE(app, "03 runtime constructors\n");
    const uint32_t result = sb4HostRuntimeInitialize(compat, &hooks);
    if (!result) WORKER_STAGE(app, "04 runtime ready\n");
    return result == SB4_HOST_RUNTIME_RETAINED ? (uint32_t)PS2_SB4_SESSION_RETAINED : result;
}

uint64_t ps2_sb4_hosted_entry(void *context, uint64_t reserved) {
    static const Ps2Sb4EngineRuntime runtime = {
        .initialize = initialize_runtime,
        .quiesce = sb4HostPosixQuiesce,
        .finalize = sb4HostRuntimeFinalize,
        .can_read_storage_from_threads = sb4HostPosixCanReadStorageFromThreads,
    };
    Ps2Sb4Worker *worker = context;
    if (!reserved && worker && worker->app) WORKER_STAGE(worker->app, "01 worker entered\n");
    return ps2_sb4_engine_entry_with_runtime(context, reserved, &runtime);
}
