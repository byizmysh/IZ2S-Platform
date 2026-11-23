#ifndef PS2_SHELL_H
#define PS2_SHELL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PS2_SHELL_VERSION 1u
#define PS2_SHELL_WIDTH 960u
#define PS2_SHELL_HEIGHT 540u
#define PS2_SHELL_CIRCLE (1u << 5) /* original DualShock2 bind index */

enum Ps2ShellEvent { PS2_SHELL_EVENT_NONE = 0, PS2_SHELL_EVENT_EXIT = 1 };
enum Ps2ShellResult {
    PS2_SHELL_OK = 0, PS2_SHELL_ARGUMENT = 1, PS2_SHELL_PRESENT_FAILED = 2,
    PS2_SHELL_POLL_FAILED = 3, PS2_SHELL_WAIT_FAILED = 4,
    PS2_SHELL_FINISH_UNPROVED = 5, PS2_SHELL_INPUT_INVALID = 6
};
enum Ps2ShellLifecycle {
    PS2_SHELL_NOT_STARTED = 0, PS2_SHELL_ACTIVE = 1,
    PS2_SHELL_FINISHED = 2, PS2_SHELL_RETAINED = 3
};
enum Ps2ShellExitReason {
    PS2_SHELL_EXIT_NONE = 0, PS2_SHELL_EXIT_HOST = 1, PS2_SHELL_EXIT_CIRCLE = 2
};

typedef struct Ps2ShellFrame {
    uint32_t version, size;
    const uint8_t* pixels;
    size_t bytes, pitch;
    uint32_t width, height;
} Ps2ShellFrame;

typedef struct Ps2ShellInput {
    uint32_t version, size;
    uint32_t buttons, connected, event, reserved;
} Ps2ShellInput;

typedef struct Ps2ShellHost {
    uint32_t version, size;
    uint8_t* pixels;
    size_t bytes, pitch;
    void* context;
    // Every callback is synchronous and returns 0 on success. The host copies
    // frame/input descriptors and never retains their temporary addresses.
    // Successful present has finished reading the caller's RGBA bytes, while
    // host presentation ownership is retained until finish confirms release.
    int (*present)(void*, const Ps2ShellFrame*);
    // Copy one current host input/event snapshot. Set reserved=0. Disconnect
    // disarms Circle until a connected neutral sample is observed again.
    int (*poll)(void*, Ps2ShellInput*);
    int (*wait)(void*, uint32_t milliseconds);
    // Called exactly once after ANY present attempt, including failed present.
    // 0 confirms no presentation resources/source bytes remain borrowed.
    // A nonzero/uncertain outcome retains ownership and forbids host teardown.
    int (*finish)(void*);
} Ps2ShellHost;

typedef struct Ps2ShellStatus {
    uint32_t version, size;
    uint32_t result, lifecycle, exit_reason;
    int32_t operation_error, finish_error;
    uint32_t ownership_retained;
    uint64_t presented, polls, waits, finish_calls;
} Ps2ShellStatus;

// Ordinary application shell: no VM, BIOS, game, JIT, filesystem, audio,
// allocation, threads or TLS. The host already owns its presentation/input
// providers and must keep them resident until finish confirms release.
// Initialize host/status version and size; input descriptors are initialized
// before every poll. The RGBA960x540 buffer is caller-owned and nonoverlapping
// with host/status. Row padding is preserved. All pointers/callbacks must be
// valid for the entire synchronous run; callbacks must not throw/unwind.
// Initial held Circle/EXIT is ignored. After a neutral event, host EXIT exits;
// Circle additionally requires a connected neutral sample and a fresh press.
// No frame/time limit silently ends a shell: tests exit through host events.
int ps2_shell_run(const Ps2ShellHost* host, Ps2ShellStatus* status);

#ifdef __cplusplus
}
#endif
#endif
