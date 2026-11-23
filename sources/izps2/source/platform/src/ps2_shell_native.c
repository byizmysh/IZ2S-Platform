#include "ps2_shell_native.h"

typedef struct Bridge {
    Ps2ShellNativeHost host;
    uint32_t observed_poison;
} Bridge;

static int bridge_present(void* context, const Ps2ShellFrame* frame) {
    Bridge* bridge = context;
    const int result = ps2_native_video_present(bridge->host.video,
        frame->pixels, frame->bytes, frame->width, frame->height, frame->pitch, 0);
    bridge->observed_poison |= bridge->host.video->poisoned;
    return result;
}
static int bridge_poll(void* context, Ps2ShellInput* input) {
    Bridge* bridge = context;
    Ps2NativePadFrame frame = {0};
    const uint32_t result = ps2_native_pad_read(bridge->host.pad, &frame);
    if (result) return (int)result;
    input->buttons = frame.buttons;input->connected = frame.connected;
    return bridge->host.exit_event(bridge->host.context, &input->event);
}
static int bridge_wait(void* context, uint32_t milliseconds) {
    const Bridge* bridge = context;
    return bridge->host.wait(bridge->host.context, milliseconds);
}
static int bridge_finish(void* context) {
    Bridge* bridge = context;
    bridge->observed_poison |= bridge->host.video->poisoned;
    const int result = bridge->host.finish(bridge->host.context, bridge->host.video);
    bridge->observed_poison |= bridge->host.video->poisoned;
    if (result) return result;
    return bridge->observed_poison ? PS2_SHELL_NATIVE_VIDEO_RETAINED : 0;
}
static int span(uintptr_t address, size_t bytes) {
    return address && bytes && bytes <= UINTPTR_MAX - address;
}
static int ready_video(const Ps2NativeVideo* video) {
    if (video->poisoned || !video->device || !video->queue || !video->swapchain ||
        !video->acquire_semaphore || !video->acquire || !video->submit ||
        !video->present || !video->wait_idle || !video->mapped ||
        (uintptr_t)video->mapped % 4 || !video->image_count || video->image_count > 8 ||
        video->max_width < PS2_SHELL_WIDTH || video->max_width > PS2_VIDEO_MAX_WIDTH ||
        video->max_height < PS2_SHELL_HEIGHT || video->max_height > PS2_VIDEO_MAX_HEIGHT ||
        video->mapped_bytes < PS2_VIDEO_HEADER_BYTES + (size_t)PS2_SHELL_WIDTH * PS2_SHELL_HEIGHT * 4)
        return 0;
    for (unsigned i = 0; i < video->image_count; ++i)
        if (!video->commands[i] || !video->semaphores[i]) return 0;
    return 1;
}
int ps2_shell_native_run(const Ps2ShellNativeHost* supplied, Ps2ShellStatus* status) {
    if (!supplied || !status || (uintptr_t)supplied % _Alignof(Ps2ShellNativeHost) ||
        (uintptr_t)status % _Alignof(Ps2ShellStatus) ||
        supplied->version != PS2_SHELL_NATIVE_VERSION || supplied->size != sizeof(*supplied) ||
        status->version != PS2_SHELL_VERSION || status->size != sizeof(*status))
        return PS2_SHELL_ARGUMENT;
    const Ps2ShellNativeHost host = *supplied;
    if (!host.video || !host.pad || (uintptr_t)host.video % _Alignof(Ps2NativeVideo) ||
        (uintptr_t)host.pad % _Alignof(Ps2NativePad) ||
        !host.wait || !host.exit_event || !host.finish || !ready_video(host.video) ||
        !host.pad->ready || host.pad->busy ||
        host.pitch < (size_t)PS2_SHELL_WIDTH * 4 ||
        host.pitch > (SIZE_MAX - (size_t)PS2_SHELL_WIDTH * 4) / (PS2_SHELL_HEIGHT - 1u) ||
        host.bytes < (PS2_SHELL_HEIGHT - 1u) * host.pitch + (size_t)PS2_SHELL_WIDTH * 4)
        return PS2_SHELL_ARGUMENT;
    const uintptr_t addresses[] = {(uintptr_t)supplied, (uintptr_t)status,
        (uintptr_t)host.video, (uintptr_t)host.pad, (uintptr_t)host.pixels,
        (uintptr_t)host.video->mapped};
    const size_t lengths[] = {sizeof(*supplied), sizeof(*status), sizeof(*host.video),
        sizeof(*host.pad), host.bytes, host.video->mapped_bytes};
    for (unsigned i = 0; i < 6; ++i) {
        if (!span(addresses[i], lengths[i])) return PS2_SHELL_ARGUMENT;
        for (unsigned j = 0; j < i; ++j)
            if (addresses[i] < addresses[j] + lengths[j] && addresses[j] < addresses[i] + lengths[i])
                return PS2_SHELL_ARGUMENT;
    }
    for (unsigned i = 0; i < 5; ++i)
        if (!host.pad->api.get[i]) return PS2_SHELL_ARGUMENT;
    Bridge bridge = {host, 0};
    const Ps2ShellHost shell = {PS2_SHELL_VERSION, sizeof(shell), host.pixels,
        host.bytes, host.pitch, &bridge, bridge_present, bridge_poll, bridge_wait, bridge_finish};
    return ps2_shell_run(&shell, status);
}
