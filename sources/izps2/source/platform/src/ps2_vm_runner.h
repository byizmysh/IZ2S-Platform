#ifndef PS2_VM_RUNNER_H
#define PS2_VM_RUNNER_H

#include <cstdint>
#include <string>

struct VMBootParameters;
class MemorySettingsInterface;
struct Ps2Sb4Worker;
struct Sb4JitCacheApi;

enum class Ps2VmResult : uint32_t {
    Stopped, HardwareFailed, CpuFailed, BootFailed, Busy, Invalid, VideoFailed, InputFailed, AudioFailed,
    StorageFailed,
};

struct Ps2VmRun {
    Ps2VmResult result;
    bool cpu_attempted;
    std::string message;
};

/* The callbacks and their context remain alive on the CPU owner until the
 * VM shuts down. The poll callback is optional. The stop flag is atomic. */
struct Ps2VmControl {
    const uint32_t *stop_requested;
    void (*poll)(void *);
    void *context;
    const Sb4JitCacheApi *jit_cache = nullptr;
};

Ps2VmRun ps2_run_vm(const VMBootParameters &, void (*sleep_ns)(uint64_t), const Ps2VmControl * = nullptr);
MemorySettingsInterface &ps2_vm_configure(const char *data_root, const char *resources, const char *bios_directory,
                                        const char *card_directory = nullptr);
void ps2_vm_close_log();
/* Called by Host::PumpMessagesOnCPUThread, including during guest execution.
 * True means that the module provider owns this poll. */
bool ps2_vm_pump_module(bool host_stop);

/* The caller installs settings and libsb4 services. The VM uses the
 * worker's presentation target, which remains owned by the launcher. */
extern "C" uint32_t ps2_sb4_run_vm(Ps2Sb4Worker *, MemorySettingsInterface *,
                                   void (*poll)(void *), void *);

#endif
