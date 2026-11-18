#ifndef SB4_INPUT_H
#define SB4_INPUT_H

#include <stdint.h>

enum {
    SB4_PAD_HANDHELD, SB4_PAD_FULL, SB4_PAD_DUAL, SB4_PAD_LEFT, SB4_PAD_RIGHT,
    SB4_PAD_STYLES,
};
enum { SB4_PAD_HANDHELD_ID = 32, SB4_PAD_CONNECTED = 1, SB4_PAD_INVALID = 0xe8d1 };

/* Existing Nintendo button bits and signed sticks with positive Y up. */
typedef struct {
    uint64_t sample, buttons;
    int32_t lx, ly, rx, ry;
    uint32_t attributes, reserved;
} Sb4PadSample;

/* Optional IZlauncher import "sb4ReadPad". Use ID 32 for handheld and
 * IDs 0–7 for the other styles. A disconnected controller, lack of focus
 * or a focus transition during the read returns an empty sample.
 * The callback is safe on the active app's worker. */
typedef uint32_t (*Sb4PadReader)(uint32_t style, uint32_t id, Sb4PadSample *out);

/* Optional IZlauncher import "sb4InputFocused". Reads the focus state
 * published by the launcher independently of controller availability. */
typedef uint32_t (*Sb4InputFocusReader)(void);

#ifdef __cplusplus
extern "C" {
#endif
uint32_t sb4ReadPad(uint32_t style, uint32_t id, Sb4PadSample *out);
uint32_t sb4InputFocused(void);
#ifdef __cplusplus
}
#endif
#endif
