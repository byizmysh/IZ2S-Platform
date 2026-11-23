#ifndef PS2_SW_FRAME_MODE_H
#define PS2_SW_FRAME_MODE_H
#include "sw_merge.h"
#define PS2_SW_FRAME_MODE_TEMPORAL_BYTES (2u * 1024u * 1024u * 4u)
#ifdef __cplusplus
extern "C" {
#endif

typedef struct Ps2SwFrameModeHistory {
    uint32_t valid, frame;
    uint64_t smode2, dispfb[2], display[2];
} Ps2SwFrameModeHistory;

typedef struct Ps2SwFrameModeCircuit {
    uint32_t enabled, block, fbw, psm;
    uint64_t dispfb, display;
    Ps2SwMergeRect source, destination;
} Ps2SwFrameModeCircuit;

typedef struct Ps2SwFrameModeInfo {
    const char *reason;
    void *rgba;
    size_t rgba_bytes, pitch, required_scratch_bytes;
    uint32_t width, height, frame, interlaced, anti_blur, source_reads;
    int32_t interlace_shader, merge_result;
    uint64_t pmode, smode1, smode2, syncv, bgcolor;
    Ps2SwFrameModeCircuit circuit[2];
} Ps2SwFrameModeInfo;

enum Ps2SwFrameModeStatus {
    PS2_FRAME_MODE_OK = 0,
    PS2_FRAME_MODE_ARGUMENT = 210,
    PS2_FRAME_MODE_RENDERER = 211,
    PS2_FRAME_MODE_UNSUPPORTED = 212,
    PS2_FRAME_MODE_HISTORY = 213,
    PS2_FRAME_MODE_STORAGE = 214,
    PS2_FRAME_MODE_MERGE = 215
};

/* Ordinary software-GS adapter; call on MTGS after all relevant GIF work.
 * No file access or platform/SDK calls. A caller-owned 32-byte
 * aligned scratch allocation contains BOTH the tightly packed RGBA output and
 * block-aligned GS readback planes. The returned output never leaves scratch.
 * Same-buffer circuit reads are deduplicated; the common Persona3 640x448 case
 * needs less than the existing 4,325,376-byte scratch contract. Insufficient
 * memory returns STORAGE with required_scratch_bytes, without decoding pixels.
 *
 * Supports progressive output and consecutive stable INT=1/FFMD=0 full frames
 * ONLY when the local Automatic policy selects pass-through (shader -1).
 * The first interlaced sample and gaps require a fresh consecutive sample.
 * This legacy entry lacks actual field parity. FFMD fields, scanmask,
 * feedback, explicit interlace modes, other temporal policy,
 * wrapping and oversized readbacks return an explicit unsupported status.
 * Geometry is rebuilt from current privileged registers, with actual history
 * preserved, so GetOutput's mutable block-local rectangles are never trusted.
 * Preserve raw captures separately. This produces spatial RGB, not a GPU or
 * console presentation; info.width/height are raw PCRTC pixels. Present NTSC
 * output at its configured aspect ratio (normally 4:3) in a separate step.
 */
int ps2_sw_frame_mode_compose(Ps2SwFrameModeHistory *history, void *scratch,
                             size_t scratch_bytes, Ps2SwFrameModeInfo *info);
/* Field-aware entry: field is the actual GSvsync argument (0 or 1), never a
 * presentation/frame-counter parity. Automatic FFMD fields use CPU FastMAD,
 * with a bounded private 8 MiB history shared by the one serial MTGS owner.
 * History is allocated on the first temporal field through the ordinary
 * cached allocator and released during application static destruction.
 * An allocation failure returns STORAGE before decoding pixels.
 * The legacy entry has no field provenance and retains its old restriction.
 * Reset history->valid on an explicit state load or owner lifecycle change. */
int ps2_sw_frame_mode_compose_field(Ps2SwFrameModeHistory *history, void *scratch,
                                   size_t scratch_bytes, unsigned field,
                                   Ps2SwFrameModeInfo *info);
#ifdef __cplusplus
}
#endif
#endif
