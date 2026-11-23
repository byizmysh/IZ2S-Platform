#ifndef PS2_SW_PRESENTATION_H
#define PS2_SW_PRESENTATION_H
#include "sw_merge.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct Ps2SwPresentationOptions {
    /* Physical width/height of ONE source pixel. 0/0 means square pixels. */
    uint32_t source_pixel_aspect_num, source_pixel_aspect_den;
    /* Desired physical picture aspect. 0/0 derives it from source dimensions
     * and source_pixel_aspect; a nonzero ratio overrides that derivation. */
    uint32_t desired_aspect_num, desired_aspect_den;
    /* Physical aspect of the viewport onto which the complete target grid is
     * stretched. 0/0 uses target.width/target.height (square target pixels).
     * Set 16/9 for a transport grid filling a 16:9 screen, regardless of grid
     * dimensions. This is distinct from the source pixel aspect above. */
    uint32_t viewport_aspect_num, viewport_aspect_den;
    uint32_t border_rgb; /* R low byte, G next, B next; alpha always opaque. */
} Ps2SwPresentationOptions;

typedef struct Ps2SwPresentationInfo {
    Ps2SwMergeRect picture;
    uint32_t desired_aspect_num, desired_aspect_den;
    uint32_t viewport_aspect_num, viewport_aspect_den;
    size_t required_output_bytes;
    int32_t merge_result;
} Ps2SwPresentationInfo;

enum Ps2SwPresentationStatus {
    PS2_PRESENTATION_OK = 0,
    PS2_PRESENTATION_ARGUMENT = 220,
    PS2_PRESENTATION_STORAGE = 221,
    PS2_PRESENTATION_ALIAS = 222,
    PS2_PRESENTATION_MERGE = 223
};

/* Bounded CPU presentation only: aspect fit, nearest pixel-center scaling,
 * opaque RGB borders. This does not change or qualify GS rendering fidelity.
 * Source pixels remain unchanged. Target pixels must not overlap source; the
 * target can reuse former GS readback storage after spatial composition.
 * No allocation, filesystem or SDK/platform calls. A 640x448 source and target
 * need 2,293,760 bytes when stored separately, fitting the existing scratch.
 *
 * Images are limited to 1024x1024. Each supplied rational component must be
 * 1..65535; complete 0/0 ratios select the defaults above. Half-zero fractions
 * are errors. Null options select square source/target pixels and black borders.
 * Fit uses the largest integer picture extent that does not exceed the ideal
 * physical aspect fit, with a minimum extent of one pixel. Centering leaves
 * an unmatched odd border pixel on the right or bottom. Row padding is kept.
 * Pixel/storage errors do not modify the target. `info` is optional and must
 * reside outside the pixel allocations; descriptors/options are snapshotted.
 *
 * Example: desired=4/3, viewport=16/9, target=640x448 gives picture rect
 * [80,0,560,448]. The same desired ratio with square target pixels640x480
 * gives [0,0,640,480]. Preserve raw PCRTC output separately from this image.
 */
int ps2_sw_presentation_rgba(const Ps2SwMergeImage *source,
                           const Ps2SwMergeOutput *target,
                           const Ps2SwPresentationOptions *options,
                           Ps2SwPresentationInfo *info);
#ifdef __cplusplus
}
#endif
#endif
