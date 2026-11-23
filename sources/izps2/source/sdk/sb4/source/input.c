#include <sb4/input.h>
#include <sb4/services.h>
#include <stdatomic.h>
#include <string.h>

/* Bit zero is the focus flag. Each transition invalidates in-flight samples. */
static atomic_uint focus_state;

_Static_assert(sizeof(Sb4PadSample) == 40 && offsetof(Sb4PadSample, attributes) == 32,
               "libounce Npad sample");

void sb4SetPadFocus(uint32_t active) {
    const unsigned requested = active != 0;
    unsigned current = atomic_load_explicit(&focus_state, memory_order_relaxed);
    while ((current & 1u) != requested) {
        if (atomic_compare_exchange_weak_explicit(&focus_state, &current, current + 1u,
                                                 memory_order_release, memory_order_relaxed))
            return;
    }
}

uint32_t sb4InputFocused(void) {
    return atomic_load_explicit(&focus_state, memory_order_acquire) & 1u;
}

uint32_t sb4ReadPad(uint32_t style, uint32_t id, Sb4PadSample *out) {
    if (!out) return SB4_PAD_INVALID;
    *out = (Sb4PadSample){0};
    if (style >= SB4_PAD_STYLES || (style == SB4_PAD_HANDHELD ? id != SB4_PAD_HANDHELD_ID : id > 7))
        return SB4_PAD_INVALID;
    const unsigned focus = atomic_load_explicit(&focus_state, memory_order_acquire);
    if (!(focus & 1u)) return 0;
    static const uint32_t services[SB4_PAD_STYLES] = {
        [SB4_PAD_HANDHELD] = SB4_HID_READ_HANDHELD,
        [SB4_PAD_FULL] = SB4_HID_READ_FULL_KEY,
        [SB4_PAD_DUAL] = SB4_HID_READ_JOY_DUAL,
        [SB4_PAD_LEFT] = SB4_HID_READ_JOY_LEFT,
        [SB4_PAD_RIGHT] = SB4_HID_READ_JOY_RIGHT,
    };
    IzServiceFunction read = iz_service_target(sb4GetServiceBridge(), services[style]);
    if (!read) return SB4_PAD_INVALID;
    uint64_t raw[5] = {0};
    uint64_t rc = read((uintptr_t)raw, (uintptr_t)&id, 0, 0, 0, 0, 0, 0);
    if (rc) return rc <= UINT32_MAX ? (uint32_t)rc : SB4_PAD_INVALID;
    Sb4PadSample sample;
    memcpy(&sample, raw, sizeof sample);
    if ((sample.attributes & SB4_PAD_CONNECTED) &&
        atomic_load_explicit(&focus_state, memory_order_acquire) == focus)
        *out = sample;
    return 0;
}
