#include "ps2_sb4_layout.h"
#include "ps2_sb4_session.h"
#include "ps2_sb4_vm_fs.h"
#include "ps2_vm_runner.h"
#include <atomic>
#include <cstdio>
#include <cstring>

extern "C" void izps2_boot_stage(IzNativeApp *, const char *, uint32_t);
std::string iz_take_startup_errors();
#define BOOT(text) izps2_boot_stage(nullptr, text "\n", sizeof(text))

namespace {
constexpr char data_root[] = "sdmc:" PS2_SB4_SD_ROOT "/" PS2_SB4_RUNTIME_DIR;
constexpr char resources[] = PS2_SB4_SDK_ROOT "/resources";
constexpr char virtual_root[] = PS2_SB4_SDK_ROOT;
constexpr char bios_directory[] = PS2_SB4_SDK_ROOT "/bios";
constexpr char card_directory[] = PS2_SB4_SDK_ROOT "/memcards";
constinit std::atomic<Ps2Sb4Worker *> active_worker{nullptr};

uint32_t run(Ps2Sb4Worker &worker, const Ps2Sb4EngineRuntime *runtime, bool &files_open) {
    if (ps2_sb4_worker_should_stop(&worker))
        return static_cast<uint32_t>(Ps2VmResult::Stopped);

    Ps2Sb4Platform platform{};
    BOOT("05 worker storage probe");
    const bool background_disc_reads = runtime && runtime->can_read_storage_from_threads &&
        runtime->can_read_storage_from_threads();
    BOOT("06 bind platform");
    const bool platform_ready = ps2_sb4_worker_platform(&worker, &platform);
    if (platform_ready) BOOT("07 install VM storage");
    if (!platform_ready ||
        !ps2_sb4_vm_fs_install(virtual_root, worker.media.iso, data_root, background_disc_reads, resources)) {
        std::fprintf(stderr, "IZPS2 VM: Cannot prepare module memory cards or open the selected disc.\n");
        return static_cast<uint32_t>(Ps2VmResult::StorageFailed);
    }
    BOOT("14 VM storage ready");
    files_open = true;
    // Keep shader and BIOS paths in the same SDK-backed file bridge as the disc.
    auto &settings = ps2_vm_configure(data_root, resources, bios_directory, card_directory);
    BOOT("20 run VM");
    const uint32_t result = ps2_sb4_run_vm(&worker, &settings, nullptr, nullptr);
    if (result == static_cast<uint32_t>(Ps2VmResult::BootFailed)) {
        const auto errors = iz_take_startup_errors();
        if (!errors.empty()) izps2_boot_stage(nullptr, errors.data(), static_cast<uint32_t>(errors.size()));
    }
    return result;
}
}

extern "C" uint64_t ps2_sb4_engine_entry_with_runtime(void *context, uint64_t reserved,
                                                      const Ps2Sb4EngineRuntime *runtime) {
    auto *worker = static_cast<Ps2Sb4Worker *>(context);
    if (reserved || !worker || !worker->app || !worker->media.bios_filename[0] ||
        !std::memchr(worker->media.bios_filename, 0, sizeof(worker->media.bios_filename)) ||
        !std::memchr(worker->media.iso, 0, sizeof(worker->media.iso)) ||
        (runtime && (!runtime->initialize || !runtime->quiesce || !runtime->finalize)))
        return PS2_SB4_SESSION_INVALID;

    Ps2Sb4Worker *previous = nullptr;
    if (!active_worker.compare_exchange_strong(previous, worker)) {
        if (previous != worker) __atomic_store_n(&worker->resources_closed, 1, __ATOMIC_RELEASE);
        return static_cast<uint32_t>(Ps2VmResult::Busy);
    }
    __atomic_store_n(&worker->resources_closed, 0, __ATOMIC_RELAXED);

    const bool cancelled = ps2_sb4_worker_should_stop(worker);
    uint32_t result = 0;
    bool runtime_live = false;
    if (!cancelled && runtime) {
        result = runtime->initialize(worker->app);
        runtime_live = result == 0;
    }
    bool files_open = false;
    if (!cancelled && !result) result = run(*worker, runtime, files_open);
    if (runtime_live && runtime->quiesce()) {
        worker->platform_retained = 1;
        std::fprintf(stderr, "IZPS2 VM: Module threads still own the runtime.\n");
        return PS2_SB4_SESSION_RETAINED;
    }
    if (result == PS2_SB4_SESSION_RETAINED) worker->platform_retained = 1;
    if (files_open) {
        // Startup settings open the log before the CPU owner exists.
        ps2_vm_close_log();
        if (worker->platform_retained || !ps2_sb4_vm_fs_finish()) {
            worker->platform_retained = 1;
            std::fprintf(stderr, "IZPS2 VM: Platform still owns live resources.\n");
        }
    }
    if (ps2_sb4_worker_release(worker) || (runtime_live && runtime->finalize())) {
        worker->platform_retained = 1;
        return PS2_SB4_SESSION_RETAINED;
    }
    __atomic_store_n(&worker->resources_closed, 1, __ATOMIC_RELEASE);
    active_worker.store(nullptr, std::memory_order_release);
    return result;
}

extern "C" uint64_t ps2_sb4_engine_entry(void *context, uint64_t reserved) {
    return ps2_sb4_engine_entry_with_runtime(context, reserved, nullptr);
}
