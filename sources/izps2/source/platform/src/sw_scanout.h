#ifndef PS2_SW_SCANOUT_H
#define PS2_SW_SCANOUT_H
#include "native_video.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct Ps2SwScanout {
    Ps2NativeVideo *video;
    void *scratch; /* 32-byte aligned, never overlaps mapped video memory. */
    size_t scratch_bytes;
    uint32_t display_index,width,height,gs_frame;
    uint64_t draws,gs_jit_bytes;
    uint32_t failure;
} Ps2SwScanout;
enum { PS2_SCANOUT_NO_DISPLAY=100, PS2_SCANOUT_UNSUPPORTED=101,
       PS2_SCANOUT_STORAGE=102, PS2_SCANOUT_RENDERER=103 };
/* Must run on the original MTGS thread after GIF work has reached it.
 * Calls the original rasterizer Sync(1), decodes actual GS VRAM with psm.rtx
 * and synchronously submits its pixels to the native shared framebuffer.
 * 0/1 explicitly chooses a raw circuit for diagnostics. 2 composes both
 * circuits through sw_frame_mode, using current PCRTC registers and the
 * local supported frame policy. Unsupported temporal/field modes fail;
 * the initial full-frame history sample returns NO_DISPLAY until ready.
 * Composed raw pixels are retained separately. Presentation is a960x54016:9
 * surface with the configured game aspect and application-menu overlay.
 * No heap allocation, filesystem, or replacement GS drawing. */
int ps2_sw_scanout_present(Ps2SwScanout *,unsigned circuit);
/* Uses actual GSvsync field parity for Automatic field deinterlacing. */
int ps2_sw_scanout_present_field(Ps2SwScanout *,unsigned circuit,unsigned field);
/* Same serial MTGS owner only. Drops retained pixels and temporal history;
 * does not reset public presentation counters or touch native GPU ownership. */
void ps2_sw_scanout_reset_history(void);
/* Redraw the retained presentation while paused, without advancing guest GS.
 * Must run on the same MTGS thread; no guest framebuffer reads occur.
 * If no guest picture is retained, a visible application menu is rendered on
 * opaque black. Hiding that menu clears its background once; later hidden
 * repeats return NO_DISPLAY until a real guest picture becomes available.
 * Menu-only pixels are never retained as a game frame/history substitute. */
int ps2_sw_scanout_repeat(Ps2SwScanout *);
#ifdef __cplusplus
}
#endif
#endif
