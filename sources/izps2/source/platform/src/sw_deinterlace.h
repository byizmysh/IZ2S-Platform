#ifndef PS2_SW_DEINTERLACE_H
#define PS2_SW_DEINTERLACE_H

#include "sw_merge.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PS2_SW_DEINTERLACE_VERSION 1u
#define PS2_SW_DEINTERLACE_MAX_WIDTH 1024u
#define PS2_SW_DEINTERLACE_MAX_HEIGHT 1024u

typedef struct Ps2SwDeinterlaceState {
    uint32_t version, size;
    uint32_t initialized, width, height, index, valid_fields, last_field;
    uint64_t sequence;
    uintptr_t history;
    size_t history_bytes;
} Ps2SwDeinterlaceState;

typedef struct Ps2SwDeinterlaceParameters {
    uint64_t sequence;
    uint32_t field, pad_first, pad_end, reserved;
} Ps2SwDeinterlaceParameters;

typedef struct Ps2SwDeinterlaceInfo {
    size_t required_history_bytes;
    uint32_t history_reset, index, bank, field, valid_fields;
    const char *reason;
} Ps2SwDeinterlaceInfo;

enum Ps2SwDeinterlaceStatus {
    PS2_DEINTERLACE_OK = 0,
    PS2_DEINTERLACE_ARGUMENT = 220,
    PS2_DEINTERLACE_STORAGE = 221,
    PS2_DEINTERLACE_ALIAS = 222,
    PS2_DEINTERLACE_STATE = 223
};

/* Initializes a caller-owned state. It does not access the old history pointer.
 * Reset before first use and when the caller changes the field composition
 * policy, display rectangles, or guest lifetime (including state load).
 * No borrowed memory is freed. Returns ARGUMENT for a null/misaligned state. */
int ps2_sw_deinterlace_reset(Ps2SwDeinterlaceState *state);

/* Scalar port of the pinned ARMSX2 FastMAD buffering/reconstruction shaders.
 * merged is the full-height spatial merge AFTER the caller applies FFMD's
 * destination shift by the actual GSvsync field. field is 0 or 1, never a frame
 * counter parity. [pad_first,pad_end) is the undrawn band for the selected
 * circuit; buffering reads its first drawn row pad_end, clamped to the image.
 * All dimensions are 1..1024. Output dimensions must equal the input.
 *
 * history is exclusive caller-owned packed RGBA, requiring 2*width*height*4
 * bytes: two full-height banks containing four interleaved fields. New state,
 * changed dimensions/workspace, a sequence gap/wrap, or repeated field parity
 * clears those bytes to RGBA zero before processing this field. Initial fields
 * therefore see zero history, just like a newly cleared upstream MAD target.
 * Sequence must advance by exactly one between consecutive alternating fields.
 *
 * State, optional info, descriptors, required history prefix and touched image spans
 * must be disjoint, except merged/output may be exactly in-place (same pixels
 * AND pitch). A partial overlap is rejected. Buffering completes all input
 * reads before reconstruction, so the exact in-place case is safe. Logical
 * spans are validated; the caller guarantees accessible mappings and stable,
 * exclusively lent storage throughout this synchronous call. Reentry on the
 * same state/workspace is forbidden. Padding and excess history bytes are not
 * modified; excess unused history capacity may alias other unused storage.
 * On any rejection, pixels, history, state and info are untouched.
 *
 * RGB decisions use the integer equivalent of sensitivity=0.08 and 0.90 from
 * the source shader. Alpha follows the selected RGBA pixel/average, but does
 * not affect motion. Averages round half upward in byte space; unspecified GPU
 * UNORM tie handling is not claimed bit-identical. No allocation, GPU calls,
 * filesystem, threads, or retained temporary descriptors are used. */
int ps2_sw_deinterlace_fastmad(Ps2SwDeinterlaceState *state,
    const Ps2SwMergeImage *merged, const Ps2SwMergeOutput *output,
    const Ps2SwDeinterlaceParameters *parameters,
    void *history, size_t history_bytes, Ps2SwDeinterlaceInfo *info);

#ifdef __cplusplus
}
#endif
#endif
