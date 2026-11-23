#ifndef PS2_RESIDENT_LAUNCHER_H
#define PS2_RESIDENT_LAUNCHER_H

#include <stdint.h>

#define PS2_RESIDENT_LAUNCHER_MAGIC UINT32_C(0x32524c50)
#define PS2_RESIDENT_MENU_BYTES UINT32_C(0x7f0000)
#define PS2_RESIDENT_PLUS_BIT (UINT32_C(1) << 10)

enum {
    PS2_RESIDENT_CONTINUE = 0, PS2_RESIDENT_SELECT_PS2 = 1,
    PS2_RESIDENT_ERROR = 2, PS2_RESIDENT_STOP = 3
};
enum {
    PS2_RESIDENT_IDLE = 0, PS2_RESIDENT_RESTORING = 1,
    PS2_RESIDENT_MENU = 2, PS2_RESIDENT_SELECTED = 3,
    PS2_RESIDENT_FAILED = 4, PS2_RESIDENT_STOPPED = 5
};

/* The existing SDK Npad state prefix and TouchScreenState<16>. */
typedef struct {
    uint64_t sample;
    uint32_t buttons, upper;
    int32_t lx, ly, rx, ry;
    uint32_t attributes, reserved[7];
} Ps2ResidentPad;
typedef struct {
    uint64_t sample;
    uint32_t count, reserved;
    uint8_t points[640];
} Ps2ResidentTouch;

/* Caller-owned supervisor storage, outside all per-game allocations/resets.
 * No field in this object is a resource-release authority. The caller must
 * join the previous game and exclusively own an idle graphics queue first. */
typedef struct {
    uint32_t magic, version, status, error;
    uint64_t snapshot, storage, storage_bytes, storage_capacity;
    uint32_t app_slot, stop_request;
    uint64_t pad[5], touch, render, render_argument, tick, sleep, tick_hz;
    uint64_t entries, restored_bytes, frames, pad_samples, touch_samples;
    uint64_t rejected, errors, late_frames, deadline, last_tick;
    uint64_t extension_flags_address;
    uint32_t observed_buttons, last_request, input_armed, period_remainder;
    uint32_t extension_flags_saved, reserved;
} Ps2ResidentLauncher;

#ifdef __cplusplus
static_assert(sizeof(Ps2ResidentPad) == 64, "SDK Npad prefix");
static_assert(sizeof(Ps2ResidentTouch) == 656, "SDK touch state");
static_assert(sizeof(Ps2ResidentLauncher) == 256, "Resident launcher ABI");
extern "C" {
#else
_Static_assert(sizeof(Ps2ResidentPad) == 64, "SDK Npad prefix");
_Static_assert(sizeof(Ps2ResidentTouch) == 656, "SDK touch state");
_Static_assert(sizeof(Ps2ResidentLauncher) == 256, "Resident launcher ABI");
#endif

/* Callback ABIs:
 * pad: void (Ps2ResidentPad*, const uint32_t* id), handheld32/styles0.
 * touch: void (Ps2ResidentTouch*), optional.
 * render: uint32_t (void*), returns0 only after queue idle.
 * tick: uint64_t (void); sleep: void (uint64_t nanoseconds).
 * All addresses must be qualified by the loader before entry. Renderer and
 * original commands/snapshot must remain alive without a PC connection.
 * Plus is suppressed; it has no emulator or resident-menu assignment.
 * run() has no frame/time limit and never polls a server. Other application
 * selections are rejected locally, as in the PS1 resident supervisor.
 */
uint32_t ps2_resident_launcher_enter(Ps2ResidentLauncher*);
uint32_t ps2_resident_launcher_frame(Ps2ResidentLauncher*);
uint32_t ps2_resident_launcher_run(Ps2ResidentLauncher*);

#ifdef __cplusplus
}
#endif

#endif
