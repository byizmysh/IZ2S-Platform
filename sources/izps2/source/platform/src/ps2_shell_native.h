#ifndef PS2_SHELL_NATIVE_H
#define PS2_SHELL_NATIVE_H

#include "ps2_shell.h"
#include "native_video.h"
#include "native_pad.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PS2_SHELL_NATIVE_VERSION 1u
#define PS2_SHELL_NATIVE_VIDEO_RETAINED 0xf801

typedef struct Ps2ShellNativeHost {
    uint32_t version, size;
    Ps2NativeVideo* video;
    Ps2NativePad* pad;
    uint8_t* pixels;
    size_t bytes, pitch;
    void* context;
    int (*wait)(void*, uint32_t milliseconds);
    // Copy PS2_SHELL_EVENT_NONE or PS2_SHELL_EVENT_EXIT. This is an ordinary
    // application/window event; it supplies no function or SDK address.
    int (*exit_event)(void*, uint32_t* event);
    // Synchronous confirmation that all borrowed presentation ownership has
    // ended, for this exact caller-owned provider. Return 0 only when proven.
    // Do not destroy/reset/unbind video, PAD, mapped storage or their handles.
    int (*finish)(void*, const Ps2NativeVideo*);
} Ps2ShellNativeHost;

// In-process adapter over ALREADY initialized caller-owned providers, used on
// their one serial owner thread. No SDK binding, HID initialization, resource
// allocation, worker, filesystem, VM, loader or target-process access occurs.
// Host/pad/video/status/pixels/video-mapped allocations must be disjoint and
// resident for this entire call; callbacks must not mutate provider identity.
// PAD remains bound and video handles/storage remain owned by the caller.
//
// The standalone shell owns input release/exit policy and RGBA rendering.
// present uses the actual native-video transport (frame tag 0, no guest frame),
// poll uses the actual native-PAD mapping before sampling the host exit event.
// The host finish callback runs exactly once after any presentation attempt.
// A poisoned video provider remains retained even if host finish returns 0;
// status then reports FINISH_UNPROVED and VIDEO_RETAINED in finish_error.
// Original provider/host errors remain in status.operation_error. Invalid
// arguments return ARGUMENT without callbacks, provider or output mutations.
// This API neither switches the application main nor proves native execution.
int ps2_shell_native_run(const Ps2ShellNativeHost*, Ps2ShellStatus*);

#ifdef __cplusplus
}
#endif
#endif
