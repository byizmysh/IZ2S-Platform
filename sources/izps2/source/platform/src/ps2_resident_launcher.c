/* Adapted from the permitted local PS1 resident-launcher.c reference,
 * SHA256 a30bd15ef16b69d4a950774a6af7985275adf515f2c75d4bc90727928a242528.
 * This menu pump has no libc, socket, allocation or host callback dependency.
 * Full PS2 resident/VM/resource integration is a separate caller contract. */
#include "ps2_resident_launcher.h"

#define EXT_FLAGS UINT64_C(0x7f000c)
#define STREAM_CLOCK (UINT64_C(0x700c80) + 240)

static void copy_bytes(void* destination, const void* source, uint64_t n) {
    volatile uint8_t* d = destination;
    const volatile uint8_t* s = source;
    while (n--) *d++ = *s++;
}
static void zero_bytes(void* destination, uint64_t n) {
    volatile uint8_t* d = destination;
    while (n--) *d++ = 0;
}
static int range(uint64_t p, uint64_t n) {
    return p && n && n <= UINT64_MAX - p;
}
static int overlaps(uint64_t p, uint64_t n, uint64_t q, uint64_t m) {
    return p < q + m && q < p + n;
}
static volatile uint32_t* word(Ps2ResidentLauncher* c, uint64_t offset) {
    return (void*)(uintptr_t)(c->storage + offset);
}
static void restore_flags(Ps2ResidentLauncher* c) {
    if (c->extension_flags_address) {
        volatile uint32_t* p = (void*)(uintptr_t)c->extension_flags_address;
        *p = (*p & ~UINT32_C(64)) | (c->extension_flags_saved & UINT32_C(64));
        c->extension_flags_address = 0;
    }
}
static uint32_t fail(Ps2ResidentLauncher* c, uint32_t error) {
    restore_flags(c);
    c->status = PS2_RESIDENT_FAILED;
    c->error = error;
    c->errors++;
    return PS2_RESIDENT_ERROR;
}
static void clear_request(Ps2ResidentLauncher* c) {
    *word(c, 1012) = 0;
    *word(c, 1016) = 0;
    zero_bytes((void*)(uintptr_t)(c->storage + 960), 16);
}

uint32_t ps2_resident_launcher_enter(Ps2ResidentLauncher* c) {
    if (!c || (uintptr_t)c % _Alignof(Ps2ResidentLauncher))
        return PS2_RESIDENT_ERROR;
    const uint64_t self = (uint64_t)(uintptr_t)c;
    if ((range(c->snapshot, c->storage_bytes) &&
         overlaps(self, sizeof(*c), c->snapshot, c->storage_bytes)) ||
        (range(c->storage, c->storage_capacity) &&
         overlaps(self, sizeof(*c), c->storage, c->storage_capacity)))
        return PS2_RESIDENT_ERROR;
    if (c->magic != PS2_RESIDENT_LAUNCHER_MAGIC || c->version != 1 ||
        c->storage_bytes != PS2_RESIDENT_MENU_BYTES ||
        c->storage_capacity < c->storage_bytes ||
        !range(c->snapshot, c->storage_bytes) ||
        !range(c->storage, c->storage_capacity) ||
        ((c->snapshot | c->storage) & 7) ||
        overlaps(c->snapshot, c->storage_bytes, c->storage, c->storage_capacity) ||
        overlaps(self, sizeof(*c), c->snapshot, c->storage_bytes) ||
        overlaps(self, sizeof(*c), c->storage, c->storage_capacity) ||
        c->app_slot >= 32 || !c->render || !c->tick || !c->sleep ||
        c->tick_hz < 60 || c->tick_hz > UINT64_C(1000000000) ||
        (c->extension_flags_address &&
         (c->storage_capacity < EXT_FLAGS + 4 ||
          c->extension_flags_address != c->storage + EXT_FLAGS)) ||
        !(c->pad[0] || c->pad[1] || c->pad[2] || c->pad[3] || c->pad[4] || c->touch)) {
        /* Invalid caller pointers must not be dereferenced by restore_flags. */
        c->status = PS2_RESIDENT_FAILED;
        c->error = 1;
        c->errors++;
        return PS2_RESIDENT_ERROR;
    }
    restore_flags(c);
    c->status = PS2_RESIDENT_RESTORING;
    c->error = 0;
    copy_bytes((void*)(uintptr_t)c->storage,
               (const void*)(uintptr_t)c->snapshot, c->storage_bytes);
    c->restored_bytes += c->storage_bytes;
    c->entries++;
    clear_request(c);
    *word(c, 92) = 0;
    *word(c, UINT64_C(0x700004)) = 0;
    c->input_armed = 0;
    c->period_remainder = 0;
    if (c->storage_capacity >= EXT_FLAGS + 4 &&
        *word(c, UINT64_C(0x7f0000)) == UINT32_C(0x48425344) &&
        *word(c, UINT64_C(0x7f0004)) == 1) {
        c->extension_flags_address = c->storage + EXT_FLAGS;
        c->extension_flags_saved = *word(c, EXT_FLAGS);
        *word(c, EXT_FLAGS) &= ~UINT32_C(64);
    }
    c->deadline = c->last_tick = ((uint64_t(*)(void))(uintptr_t)c->tick)();
    *word(c, STREAM_CLOCK + 8) = (uint32_t)c->tick_hz;
    __atomic_thread_fence(__ATOMIC_RELEASE);
    c->status = PS2_RESIDENT_MENU;
    return PS2_RESIDENT_CONTINUE;
}

static int32_t clamp_axis(int32_t value) {
    return value < -32767 ? -32767 : value > 32767 ? 32767 : value;
}
static uint32_t magnitude(int32_t value) {
    return value < 0 ? (uint32_t)(-(int64_t)value) : (uint32_t)value;
}
static void merge_axis(int32_t* out, int32_t value) {
    value = clamp_axis(value);
    if (magnitude(value) > magnitude(*out)) *out = value;
}

uint32_t ps2_resident_launcher_frame(Ps2ResidentLauncher* c) {
    if (!c || (uintptr_t)c % _Alignof(Ps2ResidentLauncher) ||
        c->status != PS2_RESIDENT_MENU) return PS2_RESIDENT_ERROR;
    if (c->stop_request) {
        restore_flags(c);
        c->status = PS2_RESIDENT_STOPPED;
        return PS2_RESIDENT_STOP;
    }
    Ps2ResidentPad aggregate;
    Ps2ResidentTouch touch;
    zero_bytes(&aggregate, sizeof aggregate);
    zero_bytes(&touch, sizeof touch);
    for (unsigned i = 0; i < 5; i++) if (c->pad[i]) {
        Ps2ResidentPad sample;
        uint32_t id = i ? 0 : 32;
        zero_bytes(&sample, sizeof sample);
        ((void(*)(Ps2ResidentPad*, const uint32_t*))(uintptr_t)c->pad[i])(&sample, &id);
        c->pad_samples++;
        if (!(sample.attributes & 1)) continue;
        if (sample.sample > aggregate.sample) aggregate.sample = sample.sample;
        aggregate.buttons |= sample.buttons;
        aggregate.upper |= sample.upper;
        aggregate.attributes |= sample.attributes;
        merge_axis(&aggregate.lx, sample.lx);
        merge_axis(&aggregate.ly, sample.ly);
        merge_axis(&aggregate.rx, sample.rx);
        merge_axis(&aggregate.ry, sample.ry);
    }
    if (c->touch) {
        ((void(*)(Ps2ResidentTouch*))(uintptr_t)c->touch)(&touch);
        c->touch_samples++;
        if (touch.count > 16) touch.count = 16;
    }
    /* The user's Plus key remains unassigned in the native PS2 surface. */
    aggregate.buttons &= ~PS2_RESIDENT_PLUS_BIT;
    c->observed_buttons |= aggregate.buttons;
    if (!c->input_armed) {
        if (!aggregate.buttons && !aggregate.upper && !touch.count)
            c->input_armed = 1;
        else {
            aggregate.buttons = aggregate.upper = 0;
            aggregate.lx = aggregate.ly = aggregate.rx = aggregate.ry = 0;
            zero_bytes(&touch, sizeof touch);
        }
    }
    copy_bytes((void*)(uintptr_t)c->storage, &aggregate, sizeof aggregate);
    copy_bytes((void*)(uintptr_t)(c->storage + 128), &touch, sizeof touch);
    uint64_t now = ((uint64_t(*)(void))(uintptr_t)c->tick)();
    if (now < c->last_tick) return fail(c, 2);
    c->last_tick = now;
    *word(c, STREAM_CLOCK) = (uint32_t)now;
    *word(c, STREAM_CLOCK + 4) = (uint32_t)(now >> 32);
    __atomic_thread_fence(__ATOMIC_RELEASE);
    const uint32_t rendered = ((uint32_t(*)(void*))(uintptr_t)c->render)(
        (void*)(uintptr_t)c->render_argument);
    if (rendered) return fail(c, UINT32_C(0x10000) | (rendered & UINT32_C(65535)));
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    c->frames++;
    const uint32_t request = *word(c, 1012);
    c->last_request = request;
    if (request) {
        clear_request(c);
        if (request == c->app_slot + 1) {
            restore_flags(c);
            c->status = PS2_RESIDENT_SELECTED;
            return PS2_RESIDENT_SELECT_PS2;
        }
        *word(c, 92) = 1;
        c->rejected++;
    }
    *word(c, 1016) = 0;
    now = ((uint64_t(*)(void))(uintptr_t)c->tick)();
    if (now < c->last_tick) return fail(c, 2);
    c->last_tick = now;
    const uint64_t step = c->tick_hz / 60;
    const uint32_t remainder = c->period_remainder + (uint32_t)(c->tick_hz % 60);
    if (c->deadline > UINT64_MAX - step - (remainder >= 60)) return fail(c, 2);
    c->deadline += step + (remainder >= 60);
    c->period_remainder = remainder >= 60 ? remainder - 60 : remainder;
    if (now < c->deadline) {
        if (c->deadline - now > UINT64_MAX / UINT64_C(1000000000))
            return fail(c, 2);
        ((void(*)(uint64_t))(uintptr_t)c->sleep)(
            (c->deadline - now) * UINT64_C(1000000000) / c->tick_hz);
    } else if (now > c->deadline) {
        c->late_frames++;
        c->deadline = now;
    }
    return PS2_RESIDENT_CONTINUE;
}

uint32_t ps2_resident_launcher_run(Ps2ResidentLauncher* c) {
    uint32_t result = ps2_resident_launcher_enter(c);
    while (result == PS2_RESIDENT_CONTINUE)
        result = ps2_resident_launcher_frame(c);
    return result;
}
