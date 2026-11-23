#ifndef PS2_CONTROLLER_SETTINGS_H
#define PS2_CONTROLLER_SETTINGS_H

#include "native_pad.h"

typedef struct {
    uint32_t swap_ab;
    uint32_t stick_dpad;
    uint32_t deadzone;
    uint32_t turbo_cross;
} Ps2PadProfile;

enum Ps2MultitapMode {
    PS2_MULTITAP_OFF,
    PS2_MULTITAP_PORT_1,
    PS2_MULTITAP_PORT_2
};

/* PCSX2 unified pad numbers: 1A, 2A, 1B–1D, 2B–2D. */
static inline unsigned ps2_controller_slots(unsigned mode, unsigned slots[PS2_PAD_PLAYERS]) {
    if (mode == PS2_MULTITAP_OFF) {
        slots[0] = 0;
        slots[1] = 1;
        return 2;
    }
    if (mode != PS2_MULTITAP_PORT_1 && mode != PS2_MULTITAP_PORT_2) return 0;
    const unsigned port = mode == PS2_MULTITAP_PORT_2;
    slots[0] = port;
    for (unsigned i = 1; i < PS2_PAD_PLAYERS; ++i)
        slots[i] = (port ? 4u : 1u) + i;
    return PS2_PAD_PLAYERS;
}

#endif
