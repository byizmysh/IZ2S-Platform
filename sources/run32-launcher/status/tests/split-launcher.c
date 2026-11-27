/* Exercise the real session loop with simulated native apps and platforms.
 * split-compositor.c separately submits the real Vulkan compositor. */
#include "../../native/split-audio.h"
#include "../../native/split-layout.h"

#include <assert.h>
#include <sb4/compat.h>
#include <sb4/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;
#define P(pointer) ((u64)(uintptr_t)(pointer))
#define W(base, offset, value) (*(u32 *)((u8 *)(base) + (offset)) = (value))
#define Q(base, offset, value) (*(u64 *)((u8 *)(base) + (offset)) = (value))
#define GET(base, offset) (*(u32 *)((u8 *)(base) + (offset)))
#define WG(base, offset) (*(u64 *)((u8 *)(base) + (offset)))
#define IZ_SHARED_BYTES 8388608u
#define IZ_SDK_INPUT_AT 0x7e0000u
enum {
    IZ_APP_NATIVE = 1, IZ_APP_AUDIO_OUT = 2, IZ_APP_ASSETS = 4,
    IZ_APP_ENGINE_IMAGE = 8, IZ_APP_COOPERATIVE = 16, IZ_APP_SB4_COMPAT = 32,
    IZ_APP_PRIVATE_128 = 64,
    IZ_NATIVE_FRAME = 2, IZ_NATIVE_STOP = 3, IZ_NATIVE_STOP_PENDING = 0x2042,
};

typedef struct {
    u32 flags, version, repeat, private_bytes, groups[3];
    char id[48], name[64];
} IzAppHeader;
typedef struct { IzAppHeader h; unsigned host; } Entry;
typedef struct { u64 commands[8], mapped, buffer, compute[4], pipeline; } Gpu;
typedef struct { unsigned unused; } IzCheckpointSession;
typedef struct IzContext IzContext;
typedef struct { IzContext *c; } IzFs;
typedef struct {
    IzFs *fs;
    struct { unsigned version; u64 frames; } app;
    bool initialized, unknown, stopping;
    unsigned slot;
    u8 *controls;
    const Sb4Compat *compat;
    Sb4AudioFunction audio[SB4_AUDIO_FUNCTIONS];
} IzNative;
struct IzContext {
    struct { IzNative native; } owner;
    u32 count, host_mode, ram_only, checkpoint_enabled, image_count, active, poisoned;
    u64 audio_functions[SB4_AUDIO_FUNCTIONS], controls, controls_buffer;
    u64 device, images[8], views[8], tick_hz, frames, checkpoint_time_bias;
    bool platform_map;
    const Sb4Compat *sb4_compat;
    u32 (*platform_restore_hid)(void);
};

enum { TEST_FRAMES = 384, TEST_START_FAILURE = 0x9999u };
typedef enum { START_OK, FAIL_COMPOSITOR, FAIL_CONTROLS, FAIL_RECORDING } StartCase;
static struct {
    int exit_slot, frame_error_slot, fail_launch_slot;
    StartCase start_case;
    bool fail_after_init;
    unsigned exit_on_input, pending_polls[2], input_count, input_limit, restores;
    unsigned launches, app_frames[2], stops[2], first_stop[2], unloaded_at[2];
    unsigned unloads[2], commands, destroyed;
    unsigned retired_commands, target_freed_at[2], private_freed_at[2], controls_freed_at;
    unsigned focus_at[TEST_FRAMES], sound_at[TEST_FRAMES];
    bool menu_at[TEST_FRAMES];
    unsigned traced_present[4], traced_closed, traced_return, cache_retirements;
    unsigned render_masks[4], presented[TEST_FRAMES], presentations, swapchain_transitions;
    bool open[2], audible[2], retain_on_stop, hold_release, hardware_focus;
    unsigned physical_mutations, system_close_poll;
    u64 pending_pcm[2];
    u64 input[TEST_FRAMES], current_buttons, forwarded[2][TEST_FRAMES];
} test;

#define IZ_SPLIT_COMPOSITOR_H
typedef struct { VkImage image; VkImageView view; } IzSplitTarget;
typedef struct { IzSplitTarget targets[2]; } IzSplitCompositor;
static VkResult split_compositor_create(IzContext *c, IzSplitCompositor *compositor)
{
    (void)c;
    assert(test.cache_retirements == 1);
    for (unsigned slot = 0; slot < 2; slot++) {
        if (slot == 1 && test.start_case == FAIL_COMPOSITOR) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
        compositor->targets[slot].image = (VkImage)(uintptr_t)(slot + 2);
    }
    return VK_SUCCESS;
}
static void split_target_destroy(VkDevice device, IzSplitTarget *target)
{
    (void)device;
    if (!target->image) return;
    const unsigned slot = P(target->image) - 2;
    assert(slot < 2 && !test.target_freed_at[slot]);
    const unsigned commands = (1u << (slot + 1)) | (1u << 3);
    if (test.presentations) assert((test.retired_commands & commands) == commands);
    test.target_freed_at[slot] = test.input_count + 1;
    target->image = VK_NULL_HANDLE;
}
static void split_compositor_destroy(VkDevice device, IzSplitCompositor *compositor)
{
    (void)device;
    assert(!compositor->targets[0].image && !compositor->targets[1].image);
    test.destroyed++;
}
static bool split_status_update(IzSplitCompositor *compositor, unsigned focus, unsigned sound,
                                 const IzSplitUi *ui, unsigned visible, const IzSplitLayout *layout)
{
    (void)compositor;
    const IzSplitLayout expected = split_layout(ui, visible);
    assert(!memcmp(layout, &expected, sizeof expected));
    test.focus_at[test.presentations] = focus;
    test.sound_at[test.presentations] = sound;
    test.menu_at[test.presentations] = ui->menu;
    return true;
}
static void split_status_name(IzSplitCompositor *compositor, unsigned slot, const char *name)
{ (void)compositor; (void)slot; (void)name; }
static void split_begin_render(VkCommandBuffer command, VkImageView view)
{
    (void)command; (void)view;
}
VKAPI_ATTR void VKAPI_CALL vkCmdEndRendering(VkCommandBuffer command)
{
    (void)command;
}
static void split_compose(VkCommandBuffer command, const IzSplitCompositor *compositor,
                          unsigned visible)
{
    (void)compositor;
    assert(P(command) > 0 && P(command) <= 3);
    test.render_masks[P(command)] = visible;
}

#define CALL(c, name, ...) ((void)(c))
#define VK(c, name, ...) ((void)(c), 0u)
static void st(void *p, size_t size, u32 value) { memset(p, 0, size); W(p, 0, value); }
static u32 command_allocate(IzContext *c, u64 *command, int secondary)
{
    (void)c; (void)secondary;
    if (test.commands == 2 && test.start_case == FAIL_RECORDING) return TEST_START_FAILURE;
    *command = ++test.commands;
    return 0;
}
static void memory_barrier(IzContext *c, u64 cmd, u64 a, u64 b, u64 d, u64 e)
{
    (void)c; (void)cmd; (void)a; (void)b; (void)d; (void)e;
}
static void image_barrier(IzContext *c, u64 cmd, u64 img, u32 from, u32 to,
                          u64 a, u64 b, u64 d, u64 e)
{
    (void)cmd; (void)b;
    if (img == c->images[0] && from == VK_IMAGE_LAYOUT_UNDEFINED) {
        assert(to == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        assert(a == VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);
        assert(d == VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);
        assert(e == VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
        test.swapchain_transitions++;
    }
}
static void bind_set(IzContext *c, Gpu *view, u64 command)
{
    (void)c; (void)view; (void)command;
}
static void dispatch(IzContext *c, u64 cmd, u64 compute, u32 x, u32 y, u32 z)
{
    (void)c; (void)cmd; (void)compute; (void)x; (void)y; (void)z;
}
static u32 private_buffer(IzContext *c, const IzAppHeader *h, Gpu *gpu)
{
    (void)c;
    gpu->mapped = P(calloc(1, h->private_bytes));
    assert(gpu->mapped);
    gpu->buffer = 2;
    return test.start_case == FAIL_CONTROLS ? TEST_START_FAILURE : 0;
}
static void free_frame_commands(IzContext *c, Gpu *commands)
{
    (void)c;
    if (commands->commands[0])
        test.retired_commands |= 1u << commands->commands[0];
    memset(commands->commands, 0, sizeof commands->commands);
}
static void destroy(IzContext *c, Gpu *gpu)
{
    (void)c;
    if (gpu->buffer == 2) {
        assert(!test.controls_freed_at && !test.open[0]);
        test.controls_freed_at = test.input_count + 1;
    } else if (gpu->buffer >= 3) {
        const unsigned slot = gpu->buffer - 3;
        assert(slot < 2 && test.unloads[slot] && !test.private_freed_at[slot]);
        test.private_freed_at[slot] = test.input_count + 1;
    }
    free((void *)(uintptr_t)gpu->mapped);
    *gpu = (Gpu){0};
}
static bool stopping(IzContext *c)
{
    (void)c;
    if (test.system_close_poll && test.input_count >= test.system_close_poll) return true;
    return test.input_count >= test.input_limit;
}
static u32 input_at(IzContext *c, u8 *controls, u64 *previous, u64 *tick, u32 epoch)
{
    (void)c; (void)previous; (void)tick; (void)epoch;
    assert(test.input_count + 1 < TEST_FRAMES);
    test.current_buttons = test.input[test.input_count++];
    Q(controls, 8, test.current_buttons);
    return 0;
}
static u32 present_at(IzContext *c, const u64 *commands, u64 controls, u32 epoch)
{
    assert(controls == c->controls && epoch == 0);
    assert(*commands > 0 && *commands <= 3);
    assert(!(test.retired_commands & (1u << *commands)));
    unsigned mask = test.render_masks[*commands];
    assert(mask && !(mask & 1 && test.stops[0]) && !(mask & 2 && test.stops[1]));
    test.presented[test.presentations++] = mask;
    if (test.system_close_poll && test.input_count >= test.system_close_poll)
        return (u32)VK_ERROR_SURFACE_LOST_KHR;
    return 0;
}

static u64 hardware_pad(u64 a, u64 b, u64 d, u64 e, u64 f, u64 g, u64 h, u64 i)
{
    (void)d; (void)e; (void)f; (void)g; (void)h; (void)i;
    assert(b);
    const Sb4PadSample sample = { .sample = 19, .buttons = test.current_buttons,
        .lx = 100, .ry = -100, .attributes = SB4_PAD_CONNECTED };
    memcpy((void *)(uintptr_t)a, &sample, sizeof sample);
    return 0;
}
static u64 hardware_styles(u64 a, u64 b, u64 d, u64 e, u64 f, u64 g, u64 h, u64 i)
{
    (void)b; (void)d; (void)e; (void)f; (void)g; (void)h; (void)i;
    *(u32 *)(uintptr_t)a = 31;
    return 0;
}
static u64 mutate_hardware(u64 a, u64 b, u64 d, u64 e, u64 f, u64 g, u64 h, u64 i)
{
    (void)a; (void)b; (void)d; (void)e; (void)f; (void)g; (void)h; (void)i;
    test.physical_mutations++;
    return SB4_PAD_INVALID;
}
static IzServiceFunction resolve_hid(u32 module, u32 offset)
{
    if (module != 5) return NULL;
    switch (offset) {
    case SB4_HID_GET_SUPPORTED_STYLES: return hardware_styles;
    case SB4_HID_SET_SUPPORTED_STYLES:
    case SB4_HID_SET_SUPPORTED_IDS:
    case SB4_HID_DISCONNECT_PAD: return mutate_hardware;
    case SB4_HID_READ_FULL_KEY:
    case SB4_HID_READ_HANDHELD:
    case SB4_HID_READ_JOY_DUAL:
    case SB4_HID_READ_JOY_LEFT:
    case SB4_HID_READ_JOY_RIGHT: return hardware_pad;
    }
    return NULL;
}
static u32 hardware_focus(void) { return test.hardware_focus; }
static u32 hardware_read(u32 style, u32 id, Sb4PadSample *sample)
{
    (void)style;
    *sample = (Sb4PadSample){0};
    if (!test.hardware_focus) return 0;
    return hardware_pad(P(sample), P(&id), 0, 0, 0, 0, 0, 0);
}
static Sb4CFunction resolve_input(const char *name)
{
    if (!strcmp(name, "sb4ReadPad")) return (Sb4CFunction)hardware_read;
    if (!strcmp(name, "sb4InputFocused")) return (Sb4CFunction)hardware_focus;
    return NULL;
}
static const Sb4Compat hardware = { .resolve_service = resolve_hid, .resolve_c = resolve_input };
static u32 restore_hid(void) { test.restores++; return 0; }

static u32 output_audible(void *unused, unsigned slot, bool audible)
{
    (void)unused;
    assert(test.open[slot]);
    test.audible[slot] = audible;
    assert(!test.audible[0] || !test.audible[1]);
    return 0;
}
static u32 output_open(void *unused, unsigned slot, bool audible)
{
    assert(!test.open[slot]);
    test.open[slot] = true;
    return output_audible(unused, slot, audible);
}
static u32 output_close(void *unused, unsigned slot)
{
    (void)unused;
    assert(test.open[slot] && !test.pending_pcm[slot]);
    test.open[slot] = test.audible[slot] = false;
    return 0;
}
static u32 output_simple(void *unused, unsigned slot)
{
    (void)unused;
    assert(test.open[slot]);
    return 0;
}
static u32 output_append(void *unused, unsigned slot, const IzSplitPcm *pcm)
{
    assert(!test.pending_pcm[slot]);
    test.pending_pcm[slot] = P(pcm);
    return output_simple(unused, slot);
}
static u32 output_flush(void *unused, unsigned slot, bool *flushed)
{
    *flushed = true;
    return output_simple(unused, slot);
}
static u32 output_released(void *unused, unsigned slot, u64 *buffer, u32 *count)
{
    *buffer = test.hold_release ? 0 : test.pending_pcm[slot];
    *count = *buffer != 0;
    if (*count) test.pending_pcm[slot] = 0;
    return output_simple(unused, slot);
}
static u32 output_count(void *unused, unsigned slot, u32 *count)
{
    /* Model the service dequeuing the PCM before returning its key. */
    *count = 0;
    return output_simple(unused, slot);
}
bool iz_split_audio_switch_begin(void)
{
    static const IzSplitAudioBackend backend = {
        output_open, output_close, output_audible, output_append, output_simple,
        output_simple, output_flush, output_released, output_count,
    };
    return iz_split_audio_begin(&backend, NULL);
}

static u32 launch_application(IzFs *fs, Entry *entry, unsigned slot, Gpu *gpu,
                              IzNative *native, IzCheckpointSession *checkpoint,
                              bool split_mode)
{
    (void)entry; (void)checkpoint;
    assert(split_mode && slot < 2);
    assert(fs->c->checkpoint_enabled == 0);
    test.launches++;
    const bool fails = (int)slot == test.fail_launch_slot;
    if (fails && !test.fail_after_init) return TEST_START_FAILURE;
    gpu->mapped = P(calloc(1, 4096));
    assert(gpu->mapped);
    gpu->buffer = 3 + slot;
    native->fs = fs;
    native->app.version = 7;
    native->initialized = true;
    native->slot = slot;
    native->controls = (void *)(uintptr_t)fs->c->controls;
    native->compat = fs->c->sb4_compat;
    for (unsigned i = 0; i < SB4_AUDIO_FUNCTIONS; i++)
        native->audio[i] = (Sb4AudioFunction)(uintptr_t)fs->c->audio_functions[i];
    u64 parameters = 0;
    assert(!native->audio[0](P(&parameters), 0, 0, 0));
    assert(!native->audio[1](P(native), P(&parameters), 0, 0));
    fs->c->active = slot + 1;
    return fails ? TEST_START_FAILURE : 0;
}
static u32 iz_native_event(IzNative *native, u32 event)
{
    const unsigned slot = native->slot;
    assert(native->initialized && test.open[slot]);
    if (event == IZ_NATIVE_FRAME) {
        assert(!test.stops[slot] && native->app.frames == ++test.app_frames[slot]);
        const u32 handheld = SB4_PAD_HANDHELD_ID;
        Sb4PadSample sampled = {0};
        IzServiceFunction pad = native->compat->resolve_service(5, SB4_HID_READ_HANDHELD);
        assert(!pad(P(&sampled), P(&handheld), 0, 0, 0, 0, 0, 0));
        assert(sampled.buttons == WG(native->controls, 8));
        test.forwarded[slot][test.input_count] = sampled.buttons;
        if ((int)slot == test.exit_slot && test.input_count == test.exit_on_input)
            W(native->controls, 1016, 1);
        if ((int)slot == test.frame_error_slot && test.input_count == test.exit_on_input)
            return 0x9876;
        return 0;
    }
    assert(event == IZ_NATIVE_STOP && native->stopping);
    assert(!test.unloads[slot]);
    if (!test.stops[slot]) test.first_stop[slot] = test.input_count;
    if (++test.stops[slot] <= test.pending_polls[slot]) return IZ_NATIVE_STOP_PENDING;
    if (!test.retain_on_stop)
        assert(!native->audio[12](P(native), 0, 0, 0));
    return 0;
}
static u32 iz_native_unload(IzNative *native)
{
    if (!native->fs) return 0;
    assert(!native->initialized && !test.open[native->slot]);
    assert(native->fs->c->platform_restore_hid == NULL);
    test.unloads[native->slot]++;
    test.unloaded_at[native->slot] = test.input_count;
    memset(native, 0, sizeof *native);
    return 0;
}

void iz2_split_trace(const char *phase, const char *app, unsigned panes, u32 result, u64 frames)
{
    (void)app;
    if (!strcmp(phase, "split-present")) {
        assert(panes && panes < 4 && !result);
        assert(test.presentations && test.presented[test.presentations - 1] == panes);
        unsigned matching = 0;
        for (unsigned i = 0; i < test.presentations; i++) matching += test.presented[i] == panes;
        assert(frames == matching);
        test.traced_present[panes]++;
    }
    if (!strcmp(phase, "split-closed")) test.traced_closed |= panes;
    if (!strcmp(phase, "split-return")) test.traced_return++;
}

static void iz2_menu_cache_retire(void) { test.cache_retirements++; }
#include "../../native/split-mode.h"

typedef enum { CLOSE_FAST, CLOSE_SLOW_ONE, CLOSE_SLOW_BOTH } CloseCase;

static IzContext make_context(void *controls)
{
    IzContext context = {
        .count = 2, .checkpoint_enabled = 1, .image_count = 1, .images = {1},
        .controls = P(controls), .controls_buffer = 1, .tick_hz = 19200000,
        .platform_map = true, .sb4_compat = &hardware, .platform_restore_hid = restore_hid,
    };
    for (unsigned i = 0; i < SB4_AUDIO_FUNCTIONS; i++) context.audio_functions[i] = i + 8;
    return context;
}

static const Entry split_apps[2] = {
    { .h = { .flags = IZ_APP_NATIVE | IZ_APP_AUDIO_OUT | IZ_APP_ASSETS | IZ_APP_ENGINE_IMAGE |
                    IZ_APP_COOPERATIVE | IZ_APP_SB4_COMPAT,
             .version = 2, .private_bytes = 1, .id = "iztwins2" } },
    { .h = { .flags = IZ_APP_NATIVE | IZ_APP_AUDIO_OUT | IZ_APP_ASSETS | IZ_APP_ENGINE_IMAGE |
                    IZ_APP_COOPERATIVE | IZ_APP_SB4_COMPAT,
             .version = 2, .private_bytes = 1, .id = "izpsx" } },
};

static void exercise(int exit_slot, int frame_error_slot, int fail_launch_slot,
                     bool swapped, CloseCase closing)
{
    memset(&test, 0, sizeof test);
    test.exit_slot = exit_slot;
    test.frame_error_slot = frame_error_slot;
    test.fail_launch_slot = fail_launch_slot;
    test.exit_on_input = 7;
    test.pending_polls[0] = closing == CLOSE_FAST ? 2 : 320;
    test.pending_polls[1] = 2;
    test.input_limit = closing == CLOSE_FAST ? 10 : 345;
    test.input[1] = 0x1000;
    test.input[2] = SPLIT_PAD_MENU;
    test.input[3] = SPLIT_PAD_X;
    test.input[4] = SPLIT_PAD_Y;
    test.input[5] = closing == CLOSE_SLOW_BOTH ? 1u << 12 : SPLIT_PAD_B;
    test.input[7] = closing == CLOSE_SLOW_BOTH ? SPLIT_PAD_A : 0x2000;
    test.hardware_focus = true;
    u8 *controls = calloc(1, IZ_SHARED_BYTES);
    assert(controls);
    IzContext context = make_context(controls);
    IzFs fs = { &context };
    Gpu ordinary = {0};
    Entry pair[2];
    IzSplitSelection selection = {0};
    assert(split_pick(&selection, &split_apps[swapped], pair) == SPLIT_PICK_LEFT);
    assert(split_pick(&selection, &split_apps[!swapped], pair) == SPLIT_PICK_PAIR);
    assert(!strcmp(pair[0].h.id, split_apps[swapped].h.id));
    u32 result = run_split(&context, &fs, pair, &ordinary, SPLIT_PAD_A);
    assert(result == (fail_launch_slot < 0 ? 0u : TEST_START_FAILURE));
    assert(!context.poisoned && !context.active && !split_input);
    assert(context.controls == P(controls) && context.controls_buffer == 1);
    assert(context.sb4_compat == &hardware && context.checkpoint_enabled == 1);
    assert(context.platform_restore_hid == restore_hid && test.restores == 1);
    for (unsigned i = 0; i < SB4_AUDIO_FUNCTIONS; i++) assert(context.audio_functions[i] == i + 8);
    assert(!test.open[0] && !test.open[1]);
    assert(test.launches == 2 && test.destroyed == 1 && test.unloads[0] == 1);
    assert(test.controls_freed_at == test.unloaded_at[0] + 1);
    for (unsigned slot = 0; slot < 2; slot++) {
        assert(test.target_freed_at[slot]);
        if (test.unloads[slot]) {
            assert(test.private_freed_at[slot] == test.unloaded_at[slot] + 1);
            assert(test.target_freed_at[slot] == test.private_freed_at[slot]);
        }
    }
    if (closing == CLOSE_SLOW_BOTH) {
        assert(test.unloads[1] == 1 && test.presentations == 7);
        assert(test.first_stop[0] == 8 && test.first_stop[1] == 8);
        assert(test.unloaded_at[0] == 328 && test.unloaded_at[1] == 10);
    } else if (fail_launch_slot < 0) {
        assert(test.swapchain_transitions == 3);
        const unsigned left_only = SPLIT_LEFT, right_only = SPLIT_RIGHT;
        const unsigned survivor = exit_slot == 1 || frame_error_slot == 1 ? left_only : right_only;
        assert(test.unloads[1] == 1 && test.presentations + 1 == test.input_limit);
        for (unsigned i = 0; i < 6; i++) assert(test.presented[i] == SPLIT_BOTH);
        for (unsigned i = 6; i < test.presentations; i++) assert(test.presented[i] == survivor);
        assert(test.forwarded[0][2] == 0 && test.forwarded[1][2] == 0x1000);
        assert(test.forwarded[0][5] == 0 && test.forwarded[1][5] == 0);
        assert(test.forwarded[0][6] == 0 && test.forwarded[1][6] == 0);
        assert(test.forwarded[0][8] == (survivor == SPLIT_LEFT ? 0x2000 : 0));
        assert(test.forwarded[1][8] == (survivor == SPLIT_RIGHT ? 0x2000 : 0));
        assert(!test.menu_at[1] && test.menu_at[2] && test.menu_at[4] && !test.menu_at[5]);
        assert(test.focus_at[3] == SPLIT_LEFT && test.sound_at[3] == IZ_SPLIT_SOUND_RIGHT);
        assert(test.focus_at[5] == SPLIT_LEFT && test.sound_at[5] == IZ_SPLIT_SOUND_LEFT);
        assert(test.focus_at[6] == survivor && test.sound_at[6] == survivor);
    } else {
        assert(test.swapchain_transitions == 0);
        assert(test.presentations == 0 && test.unloads[1] == 0);
    }
    free(controls);
}

static void exercise_repeated_recovery(void)
{
    const struct {
        StartCase failure;
        int init_failure;
        unsigned started;
        u32 result;
    } cases[] = {
        { FAIL_COMPOSITOR, -1, 0, (u32)VK_ERROR_OUT_OF_DEVICE_MEMORY },
        { FAIL_CONTROLS, -1, 0, TEST_START_FAILURE },
        { START_OK, 0, 1, TEST_START_FAILURE },
        { START_OK, 1, 2, TEST_START_FAILURE },
        { FAIL_RECORDING, -1, 2, TEST_START_FAILURE },
        { START_OK, -1, 2, 0 },
    };
    u8 *controls = calloc(1, IZ_SHARED_BYTES);
    assert(controls);
    IzContext context = make_context(controls);
    IzFs fs = { &context };
    Gpu ordinary = {0};
    for (unsigned cycle = 0; cycle < 40; cycle++) {
        for (unsigned step = 0; step < sizeof cases / sizeof cases[0]; step++) {
            memset(&test, 0, sizeof test);
            memset(controls, 0, IZ_SHARED_BYTES);
            test.start_case = cases[step].failure;
            test.fail_launch_slot = cases[step].init_failure;
            test.fail_after_init = true;
            test.exit_slot = test.frame_error_slot = -1;
            test.pending_polls[0] = 2;
            test.input_limit = 12;
            test.input[1] = SPLIT_PAD_MENU;
            test.input[2] = 1u << 12;
            test.input[4] = SPLIT_PAD_A;
            test.hardware_focus = true;
            Entry pair[2] = {split_apps[cycle % 2], split_apps[1 - cycle % 2]};
            const u32 result = run_split(&context, &fs, pair, &ordinary, 0);
            assert(result == cases[step].result);
            assert(!context.poisoned && !context.active && !split_input && !context.owner.native.fs);
            assert(context.controls == P(controls) && context.controls_buffer == 1);
            assert(context.sb4_compat == &hardware && context.checkpoint_enabled == 1);
            assert(context.platform_restore_hid == restore_hid && test.restores == 1);
            assert(!test.open[0] && !test.open[1] && test.destroyed == 1);
            assert(test.target_freed_at[0]);
            assert((test.target_freed_at[1] != 0) == (test.start_case != FAIL_COMPOSITOR));
            assert((test.controls_freed_at != 0) == (test.start_case != FAIL_COMPOSITOR));
            assert(test.unloads[0] + test.unloads[1] == cases[step].started);
            for (unsigned i = 0; i < SB4_AUDIO_FUNCTIONS; i++) assert(context.audio_functions[i] == i + 8);
            assert(test.presentations == (result ? 0u : 4u));
        }
    }
    free(controls);
    puts("PASS: 240 sessions in one launcher context, including 200 interrupted starts and 40 clean relaunches");
}

static void exercise_hardware_trace_sequence(void)
{
    memset(&test, 0, sizeof test);
    test.exit_slot = 0;
    test.frame_error_slot = test.fail_launch_slot = -1;
    test.exit_on_input = SPLIT_STEADY_FRAMES + 2;
    test.input_limit = test.exit_on_input + 3;
    test.input[1] = SPLIT_PAD_MINUS;
    test.input[3] = SPLIT_PAD_PLUS;
    test.input[5] = test.input[6] = SPLIT_PAD_MENU;
    test.input[9] = SPLIT_PAD_MENU;
    test.hardware_focus = true;
    u8 *controls = calloc(1, IZ_SHARED_BYTES);
    assert(controls);
    IzContext context = make_context(controls);
    IzFs fs = {&context};
    Gpu ordinary = {0};
    Entry pair[] = {split_apps[0], split_apps[1]};
    assert(!run_split(&context, &fs, pair, &ordinary, 0));
    assert(!test.menu_at[1] && !test.menu_at[3]);
    assert(test.forwarded[1][2] == SPLIT_PAD_MINUS && test.forwarded[1][4] == SPLIT_PAD_PLUS);
    assert(test.menu_at[5] && test.menu_at[6] && !test.menu_at[9]);
    assert(!test.forwarded[1][6] && !test.forwarded[1][10]);
    assert(test.traced_present[SPLIT_BOTH] == 2);
    assert(test.traced_present[SPLIT_RIGHT] == 1 && !test.traced_present[SPLIT_LEFT]);
    assert(test.traced_closed == SPLIT_BOTH && test.traced_return == 1 && !context.poisoned);
    free(controls);
    puts("PASS: trace records actual dual-frame milestone, surviving pane, retirement and return");
}

static void exercise_selection(void)
{
    assert(split_menu_command(false, true, SPLIT_PAD_A, 0) == SPLIT_COMMAND_NONE);
    assert(split_menu_command(false, true, SPLIT_TOGGLE, SPLIT_PAD_R) == SPLIT_COMMAND_TOGGLE);
    assert(split_menu_command(false, true, SPLIT_TOGGLE, SPLIT_PAD_UP) == SPLIT_COMMAND_TOGGLE);
    assert(split_menu_command(true, true, SPLIT_TOGGLE, SPLIT_TOGGLE) == SPLIT_COMMAND_NONE);
    assert(split_menu_command(true, true, SPLIT_PAD_A, 0) == SPLIT_COMMAND_PICK);
    assert(split_menu_command(true, true, SPLIT_PAD_A, SPLIT_PAD_A) == SPLIT_COMMAND_NONE);
    assert(split_menu_command(true, true, SPLIT_PAD_B, 0) == SPLIT_COMMAND_CANCEL);
    assert(split_menu_command(true, false, SPLIT_PAD_B, 0) == SPLIT_COMMAND_NONE);
    Entry left = { .h = { .flags = IZ_APP_PRIVATE_128, .version = 2,
        .private_bytes = 8, .id = "left" } };
    Entry right = { .h = { .version = 1, .private_bytes = 16, .id = "right" } };
    Entry missing_private_storage = { .h = { .version = 1, .id = "shared" } };
    Entry exclusive = { .h = { .flags = IZ_APP_NATIVE, .version = 2,
        .private_bytes = 8, .id = "exclusive" } };
    Entry nro = left;
    nro.host = 3; /* Even IZA-looking metadata cannot make an NRO eligible. */
    IzSplitSelection selection = {0};
    Entry pair[2];
    assert(split_pick(&selection, NULL, pair) == SPLIT_PICK_UNSUPPORTED);
    assert(split_pick(&selection, &nro, pair) == SPLIT_PICK_UNSUPPORTED);
    assert(split_pick(&selection, &missing_private_storage, pair) == SPLIT_PICK_UNSUPPORTED);
    assert(!selection.pending);
    assert(split_pick(&selection, &left, pair) == SPLIT_PICK_LEFT);
    assert(split_pick(&selection, &left, pair) == SPLIT_PICK_DUPLICATE);
    assert(split_pick(&selection, &nro, pair) == SPLIT_PICK_UNSUPPORTED);
    assert(selection.pending);
    assert(split_pick(&selection, &exclusive, pair) == SPLIT_PICK_UNSUPPORTED);
    assert(selection.pending);
    memset(&left, 0, sizeof left); /* The catalog may change pages before the second pick. */
    assert(split_pick(&selection, &right, pair) == SPLIT_PICK_PAIR);
    assert(!selection.pending);
    assert(!strcmp(pair[0].h.id, "left") && !strcmp(pair[1].h.id, "right"));
}

static u64 hid_call(const Sb4Compat *app, u32 service, u64 a, u64 b)
{
    return app->resolve_service(5, service)(a, b, 0, 0, 0, 0, 0, 0);
}

static void exercise_pad_policies(void)
{
    memset(&test, 0, sizeof test);
    test.hardware_focus = true;
    test.current_buttons = 0x1000;
    IzSplitInput input;
    assert(split_input_start(&input, &hardware));
    const Sb4Compat *left = &input.compat[0], *right = &input.compat[1];
    const u32 handheld = SB4_PAD_HANDHELD_ID, player1 = 0;
    const u32 duplicate[] = {0, 0};
    u32 styles = 0;
    Sb4PadSample sample;
    Sb4PadReader left_read = (Sb4PadReader)left->resolve_c("sb4ReadPad");
    Sb4PadReader right_read = (Sb4PadReader)right->resolve_c("sb4ReadPad");
    Sb4InputFocusReader left_focus = (Sb4InputFocusReader)left->resolve_c("sb4InputFocused");
    Sb4InputFocusReader right_focus = (Sb4InputFocusReader)right->resolve_c("sb4InputFocused");

    split_input_set_focus(&input, SPLIT_RIGHT);
    assert(!left_focus() && right_focus());
    assert(!right_read(SB4_PAD_HANDHELD, handheld, &sample) && sample.buttons == 0x1000);
    assert(!left_read(SB4_PAD_HANDHELD, handheld, &sample) && !sample.attributes);
    assert(!hid_call(left, SB4_HID_READ_HANDHELD, P(&sample), P(&handheld)) &&
           !sample.buttons && !sample.lx && !sample.ry && sample.attributes == SB4_PAD_CONNECTED);

    assert(!hid_call(left, SB4_HID_SET_SUPPORTED_STYLES, 1, 0));
    assert(!hid_call(left, SB4_HID_SET_SUPPORTED_IDS, P(&player1), 1));
    assert(hid_call(left, SB4_HID_SET_SUPPORTED_IDS, P(duplicate), 2) == SB4_PAD_INVALID);
    assert(!hid_call(left, SB4_HID_GET_SUPPORTED_STYLES, P(&styles), 0) && styles == 1);
    assert(!hid_call(right, SB4_HID_GET_SUPPORTED_STYLES, P(&styles), 0) && styles == 31);
    assert(!hid_call(left, SB4_HID_READ_HANDHELD, P(&sample), P(&handheld)) && !sample.attributes);
    assert(!hid_call(right, SB4_HID_READ_HANDHELD, P(&sample), P(&handheld)) && sample.buttons == 0x1000);

    split_input_set_focus(&input, SPLIT_LEFT);
    assert(left_focus() && !right_focus());
    assert(!left_read(SB4_PAD_FULL, player1, &sample) && sample.buttons == 0x1000);
    assert(!hid_call(left, SB4_HID_DISCONNECT_PAD, P(&player1), 0));
    assert(!left_read(SB4_PAD_FULL, player1, &sample) && !sample.attributes);
    assert(!hid_call(right, SB4_HID_READ_FULL_KEY, P(&sample), P(&player1)) &&
           sample.attributes == SB4_PAD_CONNECTED);
    split_input_set_focus(&input, SPLIT_RIGHT);
    test.hardware_focus = false;
    assert(!right_focus());
    assert(!right_read(SB4_PAD_FULL, player1, &sample) && !sample.attributes);
    assert(!hid_call(right, SB4_HID_READ_FULL_KEY, P(&sample), P(&player1)) && !sample.buttons);
    assert(!test.physical_mutations);
    split_input = NULL;
}

static void exercise_stop_with_retained_output(void)
{
    memset(&test, 0, sizeof test);
    test.retain_on_stop = true;
    test.hold_release = true;
    assert(iz_split_audio_switch_begin());
    IzContext context = { .platform_restore_hid = restore_hid };
    IzFs fs = { &context };
    IzNative native = { .fs = &fs, .initialized = true, .app.version = 7 };
    const Sb4AudioFunction *audio = iz_split_audio_functions(IZ_SPLIT_SOUND_LEFT);
    memcpy(native.audio, audio, sizeof native.audio);
    u64 parameters = 0;
    assert(!audio[0](P(&parameters), 0, 0, 0));
    assert(!audio[1](P(&native), P(&parameters), 0, 0));
    void *samples = aligned_alloc(4096, 4096);
    assert(samples);
    IzSplitPcm buffer;
    assert(!audio[5](P(&buffer), P(samples), 4096, 512));
    assert(audio[6](P(&native), P(&buffer), 0, 0) == 1);
    assert(!audio[7](P(&native), 0, 0, 0));
    assert(split_stop_step(&context, &native) == IZ_NATIVE_STOP_PENDING);
    assert(native.fs == &fs && !native.initialized && native.stopping);
    assert(test.open[0] && !test.unloads[0] && !test.restores);
    assert(test.pending_pcm[0] == P(&buffer));

    /* When the service returns its key, the launcher can finish the close. */
    test.hold_release = false;
    assert(!split_stop_step(&context, &native) && test.unloads[0] == 1);
    assert(!test.open[0] && !test.pending_pcm[0] && test.stops[0] == 1);
    assert(!iz_split_audio_end());
    free(samples);
}

static void exercise_system_close_after_left_exit(void)
{
    memset(&test, 0, sizeof test);
    test.exit_slot = 0;
    test.frame_error_slot = test.fail_launch_slot = -1;
    test.exit_on_input = 3;
    test.input_limit = 10;
    test.system_close_poll = 5;
    test.hardware_focus = true;
    u8 *controls = calloc(1, IZ_SHARED_BYTES);
    assert(controls);
    IzContext context = make_context(controls);
    IzFs fs = {&context};
    Gpu menu = {0};
    Entry pair[2] = {split_apps[0], split_apps[1]};

    assert(run_split(&context, &fs, pair, &menu, 0) == 0);
    assert(!context.poisoned && !split_input);
    assert(test.presentations == 4);
    assert(test.presented[2] == SPLIT_RIGHT && test.presented[3] == SPLIT_RIGHT);
    assert(test.unloads[0] == 1 && test.unloads[1] == 1 && test.restores == 1);
    free(controls);
}

int main(void)
{
    exercise_system_close_after_left_exit();
    exercise_selection();
    exercise_pad_policies();
    exercise(0, -1, -1, false, CLOSE_FAST);
    exercise(1, -1, -1, true, CLOSE_FAST);
    exercise(-1, 1, -1, false, CLOSE_FAST);
    exercise(-1, -1, 1, true, CLOSE_FAST);
    exercise(-1, -1, 1, true, CLOSE_SLOW_ONE);
    exercise(0, -1, -1, true, CLOSE_SLOW_ONE);
    exercise(-1, -1, -1, false, CLOSE_SLOW_BOTH);
    exercise_stop_with_retained_output();
    exercise_hardware_trace_sequence();
    exercise_repeated_recovery();
    puts("PASS: independent STOP, delayed audio, early GPU release, surviving pane, startup rollback and re-entry");
}
