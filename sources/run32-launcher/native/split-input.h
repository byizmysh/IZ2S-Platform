#ifndef IZ_SPLIT_INPUT_H
#define IZ_SPLIT_INPUT_H

#include <sb4/compat.h>
#include <sb4/input.h>
#include <stdatomic.h>
#include <string.h>

enum {
    SPLIT_HID_GET_STYLES = SB4_PAD_STYLES,
    SPLIT_HID_SET_STYLES,
    SPLIT_HID_SET_IDS,
    SPLIT_HID_DISCONNECT,
    SPLIT_HID_SERVICES,
    SPLIT_PAD_PLUS = 1u << 10,
    SPLIT_PAD_MINUS = 1u << 11,
    SPLIT_PAD_MENU = SPLIT_PAD_PLUS | SPLIT_PAD_MINUS,
};

static inline u64 split_pad_without_menu_chord(u64 buttons)
{
    return (buttons & SPLIT_PAD_MENU) == SPLIT_PAD_MENU ? buttons & ~(u64)SPLIT_PAD_MENU : buttons;
}

typedef struct {
    atomic_uint_fast64_t ids;
    atomic_uint styles;
} IzSplitPadPolicy;

typedef struct {
    const Sb4Compat *base;
    Sb4Compat compat[2];
    IzServiceFunction pad[SB4_PAD_STYLES];
    Sb4PadReader read_pad;
    Sb4InputFocusReader hardware_focus;
    IzSplitPadPolicy policy[2];
    atomic_uint focus;
} IzSplitInput;

/* These callbacks are borrowed until both apps and their workers have stopped. */
static IzSplitInput *split_input;

static const u32 split_pad_services[SPLIT_HID_SERVICES] = {
    [SB4_PAD_HANDHELD] = SB4_HID_READ_HANDHELD,
    [SB4_PAD_FULL] = SB4_HID_READ_FULL_KEY,
    [SB4_PAD_DUAL] = SB4_HID_READ_JOY_DUAL,
    [SB4_PAD_LEFT] = SB4_HID_READ_JOY_LEFT,
    [SB4_PAD_RIGHT] = SB4_HID_READ_JOY_RIGHT,
    [SPLIT_HID_GET_STYLES] = SB4_HID_GET_SUPPORTED_STYLES,
    [SPLIT_HID_SET_STYLES] = SB4_HID_SET_SUPPORTED_STYLES,
    [SPLIT_HID_SET_IDS] = SB4_HID_SET_SUPPORTED_IDS,
    [SPLIT_HID_DISCONNECT] = SB4_HID_DISCONNECT_PAD,
};

static bool split_pad_enabled(const IzSplitPadPolicy *policy, unsigned style, u32 id)
{
    /* Npad's bit order puts FullKey before Handheld. */
    const u32 style_bits[] = { 2, 1, 4, 8, 16 };
    return (atomic_load(&policy->ids) & (UINT64_C(1) << id)) &&
           (atomic_load(&policy->styles) & style_bits[style]);
}

static bool split_pad_valid(unsigned style, u32 id)
{
    return style < SB4_PAD_STYLES &&
        (style == SB4_PAD_HANDHELD ? id == SB4_PAD_HANDHELD_ID : id < 8);
}

static u64 split_pad_call(unsigned slot, unsigned service, u64 a, u64 b)
{
    IzSplitInput *input = split_input;
    if (!input) return SB4_PAD_INVALID;
    IzSplitPadPolicy *policy = &input->policy[slot];
    if (service < SB4_PAD_STYLES) {
        const u32 *id = (const void *)(uintptr_t)b;
        u8 *out = (void *)(uintptr_t)a;
        if (!out || !id || !split_pad_valid(service, *id)) return SB4_PAD_INVALID;
        if (!split_pad_enabled(policy, service, *id)) {
            memset(out, 0, sizeof(Sb4PadSample));
            return 0;
        }
        const unsigned focus = atomic_load(&input->focus);
        u64 result = input->pad[service](a, b, 0, 0, 0, 0, 0, 0);
        /* Inactive panes can still discover their connected controllers. */
        if (!result && (!(focus & (1u << slot)) || focus != atomic_load(&input->focus) ||
                        (input->hardware_focus && !input->hardware_focus()))) {
            memset(out + offsetof(Sb4PadSample, buttons), 0,
                   offsetof(Sb4PadSample, attributes) - offsetof(Sb4PadSample, buttons));
        }
        if (!result) {
            uint64_t buttons;
            memcpy(&buttons, out + offsetof(Sb4PadSample, buttons), sizeof buttons);
            buttons = split_pad_without_menu_chord(buttons);
            memcpy(out + offsetof(Sb4PadSample, buttons), &buttons, sizeof buttons);
        }
        return result;
    }
    if (service == SPLIT_HID_GET_STYLES && a) {
        *(u32 *)(uintptr_t)a = atomic_load(&policy->styles);
        return 0;
    }
    if (service == SPLIT_HID_SET_STYLES && a <= UINT32_MAX) {
        atomic_store(&policy->styles, (u32)a);
        return 0;
    }
    if (service == SPLIT_HID_DISCONNECT && a) {
        const u32 id = *(const u32 *)(uintptr_t)a;
        if (id >= 8) return SB4_PAD_INVALID;
        atomic_fetch_and(&policy->ids, ~(UINT64_C(1) << id));
        return 0;
    }
    if (service == SPLIT_HID_SET_IDS && a && b && b <= 10) {
        const u32 *ids = (const void *)(uintptr_t)a;
        u64 mask = 0;
        for (unsigned i = 0; i < b; i++) {
            const u32 id = ids[i];
            if (id >= 8 && id != 16 && id != SB4_PAD_HANDHELD_ID)
                return SB4_PAD_INVALID;
            const u64 bit = UINT64_C(1) << id;
            if (mask & bit) return SB4_PAD_INVALID;
            mask |= bit;
        }
        atomic_store(&policy->ids, mask);
        return 0;
    }
    return SB4_PAD_INVALID;
}

static u32 split_pad_focused(unsigned slot)
{
    const IzSplitInput *input = split_input;
    return input && (atomic_load(&input->focus) & (1u << slot)) &&
        input->hardware_focus && input->hardware_focus();
}

static u32 split_read_pad(unsigned slot, u32 style, u32 id, Sb4PadSample *out)
{
    if (!out) return SB4_PAD_INVALID;
    *out = (Sb4PadSample){0};
    const IzSplitInput *input = split_input;
    if (!input || !input->read_pad || !split_pad_valid(style, id)) return SB4_PAD_INVALID;
    const unsigned focus = atomic_load(&input->focus);
    if (!(focus & (1u << slot)) || !split_pad_enabled(&input->policy[slot], style, id))
        return 0;
    u32 result = input->read_pad(style, id, out);
    if (focus != atomic_load(&input->focus)) *out = (Sb4PadSample){0};
    out->buttons = split_pad_without_menu_chord(out->buttons);
    return result;
}

#define SPLIT_HID_CALLBACK(slot, name)                                      \
    static u64 split_pad_##slot##_##name(u64 a, u64 b, u64 d, u64 e,         \
                                         u64 f, u64 g, u64 h, u64 i)        \
    {                                                                      \
        (void)d; (void)e; (void)f; (void)g; (void)h; (void)i;                \
        return split_pad_call(slot, name, a, b);                             \
    }
#define SPLIT_HID_ENTRY(slot, name) [name] = split_pad_##slot##_##name,
#define SPLIT_HID_LIST(X, slot)                                             \
    X(slot, SB4_PAD_HANDHELD) X(slot, SB4_PAD_FULL) X(slot, SB4_PAD_DUAL)    \
    X(slot, SB4_PAD_LEFT) X(slot, SB4_PAD_RIGHT)                             \
    X(slot, SPLIT_HID_GET_STYLES) X(slot, SPLIT_HID_SET_STYLES)             \
    X(slot, SPLIT_HID_SET_IDS) X(slot, SPLIT_HID_DISCONNECT)

#define SPLIT_INPUT_CALLBACKS(slot)                                        \
    SPLIT_HID_LIST(SPLIT_HID_CALLBACK, slot)                                \
    static IzServiceFunction split_service_##slot(u32 module, u32 id)       \
    {                                                                      \
        static const IzServiceFunction methods[SPLIT_HID_SERVICES] = {      \
            SPLIT_HID_LIST(SPLIT_HID_ENTRY, slot)                            \
        };                                                                 \
        if (module == 5) {                                                 \
            for (unsigned n = 0; n < SPLIT_HID_SERVICES; n++)                \
                if (split_pad_services[n] == id) return methods[n];         \
        }                                                                  \
        return split_input ? split_input->base->resolve_service(module, id) : NULL; \
    }                                                                      \
    static u32 split_focus_##slot(void) { return split_pad_focused(slot); } \
    static u32 split_read_##slot(u32 style, u32 id, Sb4PadSample *out)       \
    {                                                                      \
        return split_read_pad(slot, style, id, out);                        \
    }                                                                      \
    static Sb4CFunction split_resolve_##slot(const char *name)               \
    {                                                                      \
        if (!split_input || !name) return NULL;                            \
        if (!strcmp(name, "sb4ReadPad"))                                    \
            return split_input->read_pad ? (Sb4CFunction)split_read_##slot : NULL; \
        if (!strcmp(name, "sb4InputFocused"))                               \
            return split_input->hardware_focus ? (Sb4CFunction)split_focus_##slot : NULL; \
        return split_input->base->resolve_c(name);                          \
    }
SPLIT_INPUT_CALLBACKS(0)
SPLIT_INPUT_CALLBACKS(1)
#undef SPLIT_INPUT_CALLBACKS
#undef SPLIT_HID_LIST
#undef SPLIT_HID_ENTRY
#undef SPLIT_HID_CALLBACK

static bool split_input_start(IzSplitInput *input, const Sb4Compat *base)
{
    if (!base || !base->resolve_service || !base->resolve_c || split_input) return false;
    for (unsigned i = 0; i < SB4_PAD_STYLES; i++) {
        input->pad[i] = base->resolve_service(5, split_pad_services[i]);
        if (!input->pad[i]) return false;
    }
    IzServiceFunction get_styles = base->resolve_service(5, SB4_HID_GET_SUPPORTED_STYLES);
    u32 styles = 0;
    if (!get_styles || get_styles((uintptr_t)&styles, 0, 0, 0, 0, 0, 0, 0)) return false;
    for (unsigned slot = 0; slot < 2; slot++) {
        atomic_init(&input->policy[slot].ids, (UINT64_C(1) << SB4_PAD_HANDHELD_ID) | 0xffu);
        atomic_init(&input->policy[slot].styles, styles);
        input->compat[slot] = *base;
    }
    atomic_init(&input->focus, 0);
    input->base = base;
    input->read_pad = (Sb4PadReader)base->resolve_c("sb4ReadPad");
    input->hardware_focus = (Sb4InputFocusReader)base->resolve_c("sb4InputFocused");
    input->compat[0].resolve_service = split_service_0;
    input->compat[1].resolve_service = split_service_1;
    input->compat[0].resolve_c = split_resolve_0;
    input->compat[1].resolve_c = split_resolve_1;
    split_input = input;
    return true;
}

static void split_input_set_focus(IzSplitInput *input, unsigned pane)
{
    const unsigned state = atomic_load(&input->focus);
    if ((state & SPLIT_BOTH) != pane)
        atomic_store(&input->focus, ((state + 4u) & ~(unsigned)SPLIT_BOTH) | pane);
}

#endif
