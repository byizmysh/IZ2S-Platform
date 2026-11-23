#ifndef PS2_SW_MERGE_H
#define PS2_SW_MERGE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Rectangles are half-open. Destination coordinates may be negative. */
typedef struct Ps2SwMergeRect {
    int32_t left, top, right, bottom;
} Ps2SwMergeRect;

/* Pixels are byte-ordered R,G,B,A. Padding and unaligned pointers are allowed. */
typedef struct Ps2SwMergeImage {
    const uint8_t *pixels;
    size_t bytes, pitch;
    uint32_t width, height;
} Ps2SwMergeImage;

typedef struct Ps2SwMergeLayer {
    Ps2SwMergeImage image;
    Ps2SwMergeRect source, destination;
    uint32_t enabled;
} Ps2SwMergeLayer;

typedef struct Ps2SwMergeOutput {
    uint8_t *pixels;
    size_t bytes, pitch;
    uint32_t width, height;
} Ps2SwMergeOutput;

enum Ps2SwMergeMode {
    PS2_SW_MERGE_INTERLACED = 1u << 0,
    PS2_SW_MERGE_FEEDBACK = 1u << 1,
    PS2_SW_MERGE_SCANMASK = 1u << 2
};

typedef struct Ps2SwMergeParameters {
    Ps2SwMergeLayer circuit[2];
    /* R in bits 0..7, G in 8..15, B in 16..23; high byte ignored. */
    uint32_t background_rgb;
    uint32_t mmod, alp, slbg;
    uint32_t modes;
} Ps2SwMergeParameters;

enum Ps2SwMergeResult {
    PS2_SW_MERGE_OK = 0,
    PS2_SW_MERGE_INVALID = 1,
    PS2_SW_MERGE_STORAGE = 2,
    PS2_SW_MERGE_ALIAS = 3,
    PS2_SW_MERGE_UNSUPPORTED = 4
};

/* Portable spatial RGB composition, without allocation or platform services.
 * Clear to BGCOLOR; copy circuit[1] unless SLBG=1; blend circuit[0] over it.
 * MMOD=1 uses ALP/255, MMOD=0 uses min(2*source alpha,255)/255. RGB rounds to
 * nearest integer. These are the normalized equations of the local ARMSX2
 * merge shaders, not a claim of hardware-exact fixed-point rounding.
 * Nearest sampling uses pixel centers in the ORIGINAL destination rectangle,
 * so clipping preserves its sampling phase. Source rectangles must fit their
 * images; destination rectangles are clipped to the output. Output alpha is
 * always 255, so PMODE.AMOD alpha provenance is intentionally not represented.
 *
 * Enabled inputs are validated even when SLBG removes circuit[1]. Disabled
 * inputs are ignored. Source images may alias each other, but no source image
 * may overlap the output storage span. The destination is untouched on error,
 * and row padding is always preserved. Images must have nonzero dimensions
 * no greater than INT32_MAX; all storage lengths and address ranges are checked.
 * Any modes bit, including unknown bits, returns UNSUPPORTED. Interlacing,
 * field shifts, feedback/EXTWRITE, scanmask, filtering and GS memory decoding
 * must be handled by a caller before this restricted compositor is used.
 */
int ps2_sw_merge_rgba(const Ps2SwMergeOutput *output,
                      const Ps2SwMergeParameters *parameters);

#ifdef __cplusplus
}
#endif
#endif
