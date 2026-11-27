/* Included by the native launcher after its existing loader and Vulkan helpers. */
#include "split-audio.h"
#include "split-layout.h"
#include "split-compositor.h"
#include "split-windows.h"
#include "split-input.h"
#include <stdio.h>

extern void iz2_split_trace(const char *phase, const char *app, unsigned panes,
                            u32 result, u64 frames);
extern void iz_ounce_split_status(const char *title, const char *instructions);

enum {
    SPLIT_PAD_A = 1u << 0,
    SPLIT_PAD_B = 1u << 1,
    SPLIT_PAD_X = 1u << 2,
    SPLIT_PAD_Y = 1u << 3,
    SPLIT_PAD_R = 1u << 7,
    SPLIT_PAD_UP = 1u << 13,
    SPLIT_TOGGLE = SPLIT_PAD_R | SPLIT_PAD_UP,
    SPLIT_UNAVAILABLE = 0xe91a,
    SPLIT_STOP_TIMEOUT = 0xe91b,
    SPLIT_SHUTDOWN_POLLS = 300,
    SPLIT_STEADY_FRAMES = 120,
};

typedef struct {
    Gpu views[2];
    Gpu alternate_controls;
    Gpu commands[SPLIT_BOTH];
    IzSplitCompositor compositor;
    IzNative left_native;
    IzCheckpointSession checkpoints[2];
    IzSplitInput input;
    u64 presented[SPLIT_BOTH + 1];
} IzSplitSession;

static bool split_supported(const Entry *entry)
{
    if (!entry || entry->host) return false;
    const IzAppHeader *header = &entry->h;
    if (!header->id[0] || header->repeat || !header->private_bytes) return false;
    /* Pure Vulkan apps have no native lifecycle or process-wide services. */
    if (!(header->flags & ~IZ_APP_PRIVATE_128))
        return header->version == 1 || header->version == 2;

    /* Native modules must have a known, cooperative service contract. */
    const u32 flags = IZ_APP_NATIVE | IZ_APP_AUDIO_OUT | IZ_APP_ASSETS |
        IZ_APP_ENGINE_IMAGE | IZ_APP_COOPERATIVE | IZ_APP_SB4_COMPAT;
    return header->flags == flags && header->version == 2 &&
           (!strcmp(header->id, "iztwins2") || !strcmp(header->id, "izpsx"));
}

typedef struct {
    Entry left;
    bool pending;
} IzSplitSelection;

typedef enum {
    SPLIT_PICK_UNSUPPORTED,
    SPLIT_PICK_LEFT,
    SPLIT_PICK_DUPLICATE,
    SPLIT_PICK_PAIR,
} IzSplitPick;

typedef enum { SPLIT_COMMAND_NONE, SPLIT_COMMAND_TOGGLE,
               SPLIT_COMMAND_CANCEL, SPLIT_COMMAND_PICK } IzSplitCommand;

static inline IzSplitCommand split_menu_command(bool enabled, bool ready,
                                                u64 held, u64 previous)
{
    if (!ready) return SPLIT_COMMAND_NONE;
    if ((held & SPLIT_TOGGLE) == SPLIT_TOGGLE && (previous & SPLIT_TOGGLE) != SPLIT_TOGGLE)
        return SPLIT_COMMAND_TOGGLE;
    if (!enabled) return SPLIT_COMMAND_NONE;
    const u64 pressed = held & ~previous;
    if (pressed & SPLIT_PAD_B) return SPLIT_COMMAND_CANCEL;
    if (pressed & SPLIT_PAD_A) return SPLIT_COMMAND_PICK;
    return SPLIT_COMMAND_NONE;
}

static IzSplitPick split_pick(IzSplitSelection *selection, const Entry *app,
                              Entry pair[2])
{
    if (!split_supported(app)) return SPLIT_PICK_UNSUPPORTED;
    if (!selection->pending) {
        selection->left = *app;
        selection->pending = true;
        return SPLIT_PICK_LEFT;
    }
    if (!strcmp(selection->left.h.id, app->h.id)) return SPLIT_PICK_DUPLICATE;
    pair[0] = selection->left;
    pair[1] = *app;
    selection->pending = false;
    return SPLIT_PICK_PAIR;
}

static u32 split_record(IzContext *c, IzSplitSession *split,
                        const Entry entries[2], unsigned visible)
{
    Gpu *commands = &split->commands[visible - 1];
    for (unsigned image = 0; image < c->image_count; image++) {
        u32 result = command_allocate(c, &commands->commands[image], 0);
        if (result) return result;
        u64 command = commands->commands[image];
        u8 info[72];
        st(info, 32, 42);
        result = VK(c, F_BEGIN_COMMAND, command, info, 0, 0, 0, 0, 0, 0);
        if (result) return result;

        memory_barrier(c, command, 0x4888, 0x4060, 0x800, 0x60);
        for (unsigned slot = 0; slot < 2; slot++) {
            if (!(visible & (1u << slot))) continue;
            Gpu *view = &split->views[slot];
            const u32 *groups = entries[slot].h.groups;
            bind_set(c, view, command);
            dispatch(c, command, view->compute[3], 1, 1, 1);
            memory_barrier(c, command, 0x800, 0x40, 0x800, 0x60);
            dispatch(c, command, view->compute[0], groups[0], groups[1], groups[2]);
        }
        memory_barrier(c, command, 0x800, 0x40, 0x88, 0x20);

        const VkCommandBuffer vk_command = (VkCommandBuffer)(uintptr_t)command;
        for (unsigned slot = 0; slot < 2; slot++) {
            if (!(visible & (1u << slot))) continue;
            Gpu *view = &split->views[slot];
            const IzSplitTarget *target = &split->compositor.targets[slot];
            image_barrier(c, command, P(target->image), VK_IMAGE_LAYOUT_UNDEFINED,
                          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                          0, 0, 0x400, 0x100);
            split_begin_render(vk_command, target->view);
            bind_set(c, view, command);
            CALL(c, F_BIND_PIPELINE, command, 0, view->pipeline, 0, 0, 0, 0, 0);
            CALL(c, F_DRAW, command, 3, 1, 0, 0, 0, 0, 0);
            vkCmdEndRendering(vk_command);
            image_barrier(c, command, P(target->image),
                          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                          0x400, 0x100, 0x80, 0x20);
        }
        /* present_at waits for the acquired image at COLOR_ATTACHMENT_OUTPUT.
         * Chain the layout transition to that wait, while compute can run early. */
        image_barrier(c, command, c->images[image], VK_IMAGE_LAYOUT_UNDEFINED,
                      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                      VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
                      VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                      VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
        split_begin_render(vk_command, (VkImageView)(uintptr_t)c->views[image]);
        split_compose(vk_command, &split->compositor, visible);
        vkCmdEndRendering(vk_command);
        image_barrier(c, command, c->images[image], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                      VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, 0x400, 0x100, 0, 0);
        memory_barrier(c, command, 0x800, 0x40, 0x4000, 0x2000);
        result = VK(c, F_END_COMMAND, command, 0, 0, 0, 0, 0, 0, 0);
        if (result) return result;
    }
    return 0;
}

static u32 split_frame(IzNative *native)
{
    if (!native->initialized || native->stopping) return SPLIT_UNAVAILABLE;
    native->app.frames++;
    return iz_native_event(native, IZ_NATIVE_FRAME);
}

static u32 split_stop_step(IzContext *c, IzNative *native)
{
    if (c->poisoned || native->unknown) return SPLIT_UNAVAILABLE;
    if (native->initialized) {
        native->stopping = 1;
        u32 result = iz_native_event(native, IZ_NATIVE_STOP);
        if (result == IZ_NATIVE_STOP_PENDING && native->app.version < 4)
            return SPLIT_UNAVAILABLE;
        if (result) return result;
        native->initialized = 0;
    }
    IzSplitSoundSource slot = native == &c->owner.native ?
        IZ_SPLIT_SOUND_RIGHT : IZ_SPLIT_SOUND_LEFT;
    /* The service must no longer own PCM before the loader frees app memory. */
    bool draining = false;
    u32 result = iz_split_audio_retire(slot, &draining);
    if (result) return result;
    if (draining) return IZ_NATIVE_STOP_PENDING;

    /* HID is process-wide. Restore it once at split-session shutdown. */
    u32 (*restore)(void) = c->platform_restore_hid;
    c->platform_restore_hid = NULL;
    result = iz_native_unload(native);
    c->platform_restore_hid = restore;
    return result;
}

static void split_release_pane(IzContext *c, IzSplitSession *split, unsigned slot)
{
    const unsigned pane = 1u << slot;
    /* present_at has drained the last submission. Retire every command that
     * references this pane before freeing its image or private memory. */
    free_frame_commands(c, &split->commands[SPLIT_BOTH - 1]);
    free_frame_commands(c, &split->commands[pane - 1]);
    split_target_destroy((VkDevice)(uintptr_t)c->device, &split->compositor.targets[slot]);
    destroy(c, &split->views[slot]);
    if (pane == SPLIT_LEFT) destroy(c, &split->alternate_controls);
}

static u32 split_retire_windows(IzContext *c, IzSplitSession *split,
                                IzNative *natives[2], unsigned *retiring)
{
    for (unsigned slot = 0; slot < 2; slot++) {
        const unsigned pane = 1u << slot;
        if (!(*retiring & pane)) continue;
        u32 result = split_stop_step(c, natives[slot]);
        if (result == IZ_NATIVE_STOP_PENDING) continue;
        if (result) {
            iz2_split_trace("split-stop-failed", "", pane, result, split->presented[SPLIT_BOTH]);
            return result;
        }
        split_release_pane(c, split, slot);
        *retiring &= ~pane;
        iz2_split_trace("split-closed", "", pane, 0, split->presented[SPLIT_BOTH]);
        fprintf(stderr, "IZ2S_SPLIT_CLOSED slot=%u\n", slot);
    }
    return 0;
}

static u32 split_drain(IzContext *c, IzSplitSession *split,
                       IzNative *natives[2], unsigned retiring)
{
    unsigned shutdown_polls = 0;
    while (retiring) {
        /* Only applet shutdown bounds the asynchronous acknowledgement. */
        if (stopping(c) && shutdown_polls++ == SPLIT_SHUTDOWN_POLLS)
            return SPLIT_STOP_TIMEOUT;
        u32 result = split_retire_windows(c, split, natives, &retiring);
        if (result) return result;
        if (!retiring) break;
        CALL(c, F_TICK, 0, 0, 0, 0, 0, 0, 0, 0);
        CALL(c, F_SLEEP, 16666667, 0, 0, 0, 0, 0, 0, 0);
    }
    return 0;
}

static void split_neutral_input(u8 *controls)
{
    memset(controls + 8, 0, 24);
    memset(controls + 128, 0, SPLIT_TOUCH_BYTES);
    W(controls, IZ_SDK_INPUT_AT, 0);
}

static void split_select_sound(IzSplitSoundSource source)
{
    const u32 result = iz_split_audio_select(source);
    fprintf(stderr, "IZ2S_SPLIT_SOUND window=%u result=0x%08x\n",
            iz_split_audio_source(), result);
}

static u32 run_split(IzContext *c, IzFs *fs, Entry pair[2], Gpu *normal_gpu,
                     u64 held)
{
    if (!split_supported(&pair[0]) || !split_supported(&pair[1]) ||
        !strcmp(pair[0].h.id, pair[1].h.id) || split_input || c->host_mode || c->ram_only ||
        !c->platform_map || !c->sb4_compat || c->owner.native.fs)
        return SPLIT_UNAVAILABLE;

    IzSplitSession *split = calloc(1, sizeof *split);
    if (!split) return SPLIT_UNAVAILABLE;
    const Sb4Compat *normal_compat = c->sb4_compat;
    if (!split_input_start(&split->input, normal_compat)) {
        free(split);
        return SPLIT_UNAVAILABLE;
    }
    if (!iz_split_audio_switch_begin()) {
        split_input = NULL;
        free(split);
        return SPLIT_UNAVAILABLE;
    }
    iz2_menu_cache_retire();
    iz2_split_trace("split-begin", "", SPLIT_BOTH, 0, 0);

    const u64 normal_controls = c->controls;
    const u64 normal_buffer = c->controls_buffer;
    u64 normal_audio[SB4_AUDIO_FUNCTIONS];
    memcpy(normal_audio, c->audio_functions, sizeof normal_audio);
    const u32 normal_checkpoint = c->checkpoint_enabled;
    c->checkpoint_enabled = 0;
    destroy(c, normal_gpu);

    IzNative *natives[] = { &split->left_native, &c->owner.native };
    const IzAppHeader storage = { .private_bytes = IZ_SHARED_BYTES };
    u32 result = (u32)split_compositor_create(c, &split->compositor);
    if (!result) for (unsigned slot = 0; slot < 2; slot++)
        split_status_name(&split->compositor, slot, pair[slot].h.name);
    if (!result) result = private_buffer(c, &storage, &split->alternate_controls);
    if (!result) {
        for (unsigned slot = 0; slot < 2; slot++) {
            c->controls = slot ? normal_controls : split->alternate_controls.mapped;
            c->controls_buffer = slot ? normal_buffer : split->alternate_controls.buffer;
            c->sb4_compat = &split->input.compat[slot];
            const Sb4AudioFunction *functions = iz_split_audio_functions(slot + 1);
            for (unsigned i = 0; i < SB4_AUDIO_FUNCTIONS; i++)
                c->audio_functions[i] = (uintptr_t)functions[i];
            iz2_split_trace("split-app-start", pair[slot].h.id, 1u << slot, 0, 0);
            result = launch_application(fs, &pair[slot], slot,
                &split->views[slot], natives[slot], &split->checkpoints[slot], true);
            iz2_split_trace("split-app-ready", pair[slot].h.id, 1u << slot, result, 0);
            if (result) break;
        }
    }
    c->controls = normal_controls;
    c->controls_buffer = normal_buffer;
    c->sb4_compat = normal_compat;
    c->checkpoint_enabled = normal_checkpoint;
    memcpy(c->audio_functions, normal_audio, sizeof normal_audio);

    for (unsigned visible = SPLIT_LEFT; visible <= SPLIT_BOTH && !result; visible++)
        result = split_record(c, split, pair, visible);
    if (!result) fprintf(stderr, "IZ2S_SPLIT_READY left=%.48s right=%.48s\n",
                         pair[0].h.id, pair[1].h.id);

    IzSplitWindows windows = { .visible = SPLIT_BOTH, .focus = SPLIT_RIGHT };
    IzSplitUi ui = { .corner = 3, .size = 2 };
    bool suppress = true; /* The opening chord also contains a game button. */
    bool opening = true;
    u8 *right_controls = (void *)(uintptr_t)normal_controls;
    u8 *left_controls = (void *)(uintptr_t)split->alternate_controls.mapped;
    u8 *app_controls[] = { left_controls, right_controls };
    u64 last_buttons = held, shortcut_history = 0, shortcut_tick = 0;
    while (!result && !stopping(c)) {
        input_at(c, right_controls, &shortcut_history, &shortcut_tick, 0);
        const u64 buttons = WG(right_controls, 8);
        uint8_t raw_touch[SPLIT_TOUCH_BYTES];
        memcpy(raw_touch, right_controls + 128, sizeof raw_touch);
        if (!buttons) suppress = opening = false;
        unsigned sound_choice = iz_split_audio_source();
        const u64 down = buttons & ~last_buttons;
        bool menu_changed = false;
        if (!opening && (buttons & SPLIT_PAD_MENU) == SPLIT_PAD_MENU &&
            (last_buttons & SPLIT_PAD_MENU) != SPLIT_PAD_MENU) {
            ui.menu = !ui.menu;
            menu_changed = suppress = true;
        } else if (ui.menu && (down & SPLIT_PAD_B)) {
            ui.menu = false;
            menu_changed = suppress = true;
        } else if (ui.menu) {
            if (down & (1u << 12)) ui.selected = (ui.selected + SPLIT_BUTTONS - 1) % SPLIT_BUTTONS;
            if (down & (1u << 14)) ui.selected = (ui.selected + 1) % SPLIT_BUTTONS;
            unsigned action = SPLIT_BUTTONS;
            if (down & SPLIT_PAD_X) action = SPLIT_UI_FOCUS;
            else if (down & SPLIT_PAD_Y) action = SPLIT_UI_SOUND;
            else if (down & SPLIT_PAD_A) action = ui.selected;
            if (action < SPLIT_BUTTONS) split_ui_action(&ui, &windows, &sound_choice, action);
        }
        const u32 touch_count = split_touch_word(raw_touch, 8);
        if (opening || menu_changed) {
            ui.dragging = false;
            // Keep a held UI contact blocked until the finger leaves the screen.
            if (touch_count) {
                ui.held = ui.consumed = true;
                ui.touch_pane = 0;
            }
        } else if (touch_count || ui.held) {
            split_ui_touch(&ui, &windows, &sound_choice, raw_touch);
        }
        if (sound_choice != iz_split_audio_source()) split_select_sound(sound_choice);
        last_buttons = buttons;
        const bool neutral = suppress || ui.menu;

        const unsigned input_mask = neutral ? 0 : windows.focus;
        if ((windows.visible | windows.retiring) & SPLIT_LEFT) {
            memcpy(left_controls, right_controls, 64);
            Q(left_controls, 0x700d70, WG(right_controls, 0x700d70));
            W(left_controls, 0x700d78, c->tick_hz);
            split_neutral_input(left_controls);
            if (input_mask & SPLIT_LEFT)
                memcpy(left_controls + 8, right_controls + 8, 24);
        }
        split_input_set_focus(&split->input, input_mask);
        if (!(input_mask & SPLIT_RIGHT)) split_neutral_input(right_controls);
        const unsigned routed_windows = windows.visible;
        IzSplitLayout layout = split_layout(&ui, routed_windows);
        for (unsigned slot = 0; slot < 2; slot++) {
            const unsigned pane = 1u << slot;
            if ((windows.visible | windows.retiring) & pane)
                split_route_touch(&ui, &layout, windows.visible, pane, raw_touch,
                                  app_controls[slot] + 128, !(input_mask & pane));
        }

        for (unsigned slot = 0; slot < 2 && !c->poisoned; slot++) {
            const unsigned pane = 1u << slot;
            if (!(windows.visible & pane)) continue;
            u32 app_error = 0;
            if ((pair[slot].h.flags & IZ_APP_NATIVE) && !GET(app_controls[slot], 1016))
                app_error = split_frame(natives[slot]);
            if (app_error || GET(app_controls[slot], 1016)) {
                split_window_exit(&windows, pane);
                iz2_split_trace("split-app-exit", pair[slot].h.id, pane, app_error,
                                split->presented[SPLIT_BOTH]);
                fprintf(stderr, "IZ2S_SPLIT_EXIT slot=%u result=0x%08x\n",
                        slot, app_error);
            }
        }
        if (c->poisoned) break;

        result = split_retire_windows(c, split, natives, &windows.retiring);
        if (result) {
            c->poisoned = 1;
            break;
        }

        const IzSplitSoundSource sound = iz_split_audio_source();
        const IzSplitSoundSource remaining_sound = split_sound_after_exit(windows.visible, sound);
        if (remaining_sound != sound) split_select_sound(remaining_sound);
        if (!windows.visible && !windows.retiring) break;
        if (windows.visible) {
            if (windows.visible != routed_windows) layout = split_layout(&ui, windows.visible);
            split_status_update(&split->compositor, windows.focus, iz_split_audio_source(),
                                &ui, windows.visible, &layout);
            result = present_at(c, split->commands[windows.visible - 1].commands,
                                normal_controls, 0);
            if (result) {
                iz2_split_trace("split-present-failed", "", windows.visible, result,
                                split->presented[windows.visible]);
                c->poisoned = 1;
            } else {
                u64 frames = ++split->presented[windows.visible];
                if (frames == 1 || (windows.visible == SPLIT_BOTH && frames == SPLIT_STEADY_FRAMES))
                    iz2_split_trace("split-present", "", windows.visible, 0, frames);
            }
        } else {
            CALL(c, F_SLEEP, 16666667, 0, 0, 0, 0, 0, 0, 0);
        }
        c->frames++;
    }

    if (c->poisoned) {
        iz2_split_trace("split-abandon", "", windows.visible, result ? result : SPLIT_UNAVAILABLE,
                        split->presented[SPLIT_BOTH]);
        return result ? result : SPLIT_UNAVAILABLE;
    }
    split_input_set_focus(&split->input, 0);
    /* Stop both while their control and GPU buffers are still live. */
    u32 stop_error = split_drain(c, split, natives, windows.visible | windows.retiring);
    if (!stop_error) stop_error = iz_split_audio_end();
    if (!stop_error && c->platform_restore_hid)
        stop_error = c->platform_restore_hid();
    if (stop_error) {
        c->poisoned = 1;
        iz2_split_trace("split-cleanup-failed", "", windows.visible, stop_error,
                        split->presented[SPLIT_BOTH]);
        return stop_error;
    }
    split_compositor_destroy((VkDevice)(uintptr_t)c->device, &split->compositor);
    const u64 both_frames = split->presented[SPLIT_BOTH];
    split_input = NULL;
    free(split);
    c->active = 0;
    c->checkpoint_time_bias = 0;
    iz2_split_trace("split-return", "", 0, result, both_frames);
    fprintf(stderr, "IZ2S_SPLIT_RETURN result=0x%08x\n", result);
    return result;
}
