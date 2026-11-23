#ifndef PS2_SHELL_HBX_H
#define PS2_SHELL_HBX_H

#include "ps2_shell.h"
#include "hbx_sdk.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PS2_SHELL_HBX_VERSION 1u
#define PS2_SHELL_HBX_PITCH ((size_t)PS2_SHELL_WIDTH * 4u)
#define PS2_SHELL_HBX_PIXELS_BYTES (PS2_SHELL_HBX_PITCH * PS2_SHELL_HEIGHT)
/* A radius-zero SDK rect consumes six vertices. The pinned stock renderer
 * holds 163840 vertices; its public ABI has no capacity/overflow query.
 * A compatible caller must grant this entire geometry budget exclusively. */
#define PS2_SHELL_HBX_MAX_RUNS 8192u
#define PS2_SHELL_HBX_MAX_VERTICES (PS2_SHELL_HBX_MAX_RUNS * 6u)

typedef struct Ps2ShellHbxReceipt {
    uint32_t version, size;
    int32_t sdk_result, present_result;
    uint32_t shell_called, stop_at_entry, stop_during_run, begin_calls;
    uint32_t horizontal_runs, vertices, frame_before, frame_after;
    uint32_t present_calls, present_confirmed, source_bytes_released;
    uint32_t caller_renderer_retained, renderer_reuse_permitted;
    uint32_t geometry_limit_runs, geometry_limit_vertices, reserved;
} Ps2ShellHbxReceipt;

/* Ordinary synchronous application callback adapter, for hbx_app_main(ctx).
 * Calls the existing ps2_shell_run; it does not initialize the emulator.
 * HBX_B is the shell's Circle action; other held buttons block neutral arming.
 * All configured SDK pad sources are polled, using SDK stack scratch. Touch
 * and host scratch blocks are unused. Connected source count becomes boolean.
 * Initial/reconnected held B retains the shell's connected-neutral policy.
 * A stop already requested returns HBX_ERR_STOPPED before drawing. A later
 * stop interrupts polling, finishes the shell, and returns HBX_ERR_STOPPED.
 *
 * The caller owns initialized renderer/input providers, the writable context
 * and its optional control block, pixels and status. They must remain valid,
 * stable and exclusively lent for this synchronous call. The renderer must
 * support at least MAX_VERTICES per begin. No callbacks may retain the stack
 * descriptors or throw/unwind. Logical spans/ABI are checked, not OS mappings.
 * Pixel storage must be disjoint from context, UI tables, outputs and control.
 * Status must have PS2_SHELL_VERSION/sizeof(Ps2ShellStatus) initialized.
 * Optional receipt must have PS2_SHELL_HBX_VERSION/sizeof(*receipt) initialized.
 * Invalid arguments/ABI/missing operations leave pixels and outputs untouched.
 *
 * Each rect synchronously copies geometry/color; source RGBA is never handed
 * to the GPU. Successful present additionally requires frame()==before+1.
 * A failed/unconfirmed present retains the shell's presentation lease: the
 * public UI ABI cannot prove recovery/idle, and caller must keep its renderer
 * resident and resolve it. Even success never destroys caller resources or
 * proves compositor/global GPU idle. This adapter creates none of those
 * resources, threads, heaps or services. Do not reenter on the same renderer.
 * Returns an HBX_* status; the shell result remains available in status.
 */
int ps2_shell_hbx_run(const struct hbx_ctx* ctx, uint8_t* pixels, size_t bytes,
                      Ps2ShellStatus* status);
int ps2_shell_hbx_run_ex(const struct hbx_ctx* ctx, uint8_t* pixels, size_t bytes,
                         Ps2ShellStatus* status, Ps2ShellHbxReceipt* receipt);

#ifdef __cplusplus
}
#endif
#endif
