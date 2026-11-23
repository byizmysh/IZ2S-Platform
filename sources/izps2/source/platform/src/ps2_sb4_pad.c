#include "ps2_sb4_pad.h"

int ps2_sb4_pad_configure(const IzNativeApp *app, Ps2Sb4Pad *source) {
    if (!source) return 0;
    const Sb4Compat *compat = sb4ModuleCompat(app);
    source->read = compat ? (Sb4PadReader)compat->resolve_c("sb4ReadPad") : NULL;
    return source->read != NULL;
}

uint32_t ps2_sb4_pad_read(const void *context, uint32_t style, uint32_t id, Ps2NativeNpadState *out) {
    const Ps2Sb4Pad *source = context;
    if (!source || !source->read || !out || style >= SB4_PAD_STYLES) return PS2_PAD_INVALID;
    static const uint32_t styles[SB4_PAD_STYLES] = {
        [PS2_NPAD_HANDHELD] = SB4_PAD_HANDHELD,
        [PS2_NPAD_FULL] = SB4_PAD_FULL,
        [PS2_NPAD_DUAL] = SB4_PAD_DUAL,
        [PS2_NPAD_LEFT] = SB4_PAD_LEFT,
        [PS2_NPAD_RIGHT] = SB4_PAD_RIGHT,
    };
    Sb4PadSample sample = {0};
    uint32_t rc = source->read(styles[style], id, &sample);
    *out = (Ps2NativeNpadState){0};
    if (rc) return rc;
    *out = (Ps2NativeNpadState){
        .sample = sample.sample, .buttons = (uint32_t)sample.buttons,
        .upper = (uint32_t)(sample.buttons >> 32),
        .lx = sample.lx, .ly = sample.ly, .rx = sample.rx, .ry = sample.ry,
        .attributes = sample.attributes,
    };
    return 0;
}
