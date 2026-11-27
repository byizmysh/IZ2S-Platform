#ifndef IZ2_MENU_IDLE_H
#define IZ2_MENU_IDLE_H

#include <stdbool.h>
#include <stdint.h>

enum { IZ2_MENU_IDLE_SECONDS = 2, IZ2_MENU_STICK_WAKE = 6553 };
#define IZ2_MENU_IDLE_POLL_NS UINT64_C(16666667)

typedef struct {
    uint64_t quiet_since, last_draw;
    bool tracking, drew_idle;
} Iz2MenuIdle;

static inline bool iz2_menu_input_active(uint64_t buttons, uint32_t touches, const int32_t axes[4])
{
    if (buttons || touches) return true;
    for (unsigned i = 0; i < 4; i++)
        if (axes[i] > IZ2_MENU_STICK_WAKE || axes[i] < -IZ2_MENU_STICK_WAKE) return true;
    return false;
}

/* Keep polling input and services while reusing the displayed image.
 * The once-per-second refresh also covers shader state outside the native UI. */
static inline bool iz2_menu_draw(Iz2MenuIdle *idle, uint64_t now, uint32_t hz,
                                 bool quiet, bool input, bool changed)
{
    if (!quiet || input || !hz) {
        *idle = (Iz2MenuIdle){0};
        return true;
    }
    if (!idle->tracking || now < idle->quiet_since) {
        *idle = (Iz2MenuIdle){ .quiet_since = now, .tracking = true };
    }
    if (now - idle->quiet_since < (uint64_t)hz * IZ2_MENU_IDLE_SECONDS) return true;
    if (!changed && idle->drew_idle && now - idle->last_draw < hz) return false;
    idle->drew_idle = true;
    idle->last_draw = now;
    return true;
}

#endif
