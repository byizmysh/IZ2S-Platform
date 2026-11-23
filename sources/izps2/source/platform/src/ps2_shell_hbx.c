#include "ps2_shell_hbx.h"
#include "hbx_app.h"
#include "hbx_ui.h"

enum { HBX_SHELL_STOP = -101, HBX_SHELL_UNCONFIRMED = -102,
       HBX_SHELL_GEOMETRY = -103, HBX_SHELL_RETAINED = -104 };

typedef struct HbxShellBridge {
    const struct hbx_ctx* ctx;
    struct hbx_ctx input_ctx;
    struct hbx_input_state input;
    struct hbx_ui_ops ops;
    void* renderer;
    Ps2ShellHbxReceipt receipt;
} HbxShellBridge;

static int span(uintptr_t p, size_t n) {
    return p && n && n <= UINTPTR_MAX - p;
}
static int separate(uintptr_t a, size_t n, uintptr_t b, size_t m) {
    return a + n <= b || b + m <= a;
}
static int writable_separate(uintptr_t p, size_t n, uintptr_t pixels,
                            size_t bytes, uintptr_t status, uintptr_t receipt) {
    return separate(p, n, pixels, bytes) &&
        separate(p, n, status, sizeof(Ps2ShellStatus)) &&
        (!receipt || separate(p, n, receipt, sizeof(Ps2ShellHbxReceipt)));
}
static uint32_t color(const uint8_t* p) {
    return HBX_RGBA(p[0], p[1], p[2], p[3]);
}
static int same_color(const uint8_t* a, const uint8_t* b) {
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3];
}

static int present(void* context, const Ps2ShellFrame* frame) {
    HbxShellBridge* b = context;
    uint32_t runs = 0;
    /* First count the whole immutable shell image. Never partially append an
     * over-budget image to a renderer whose void rect op cannot report drops. */
    for (uint32_t y = 0; y < frame->height; ++y) {
        const uint8_t* row = frame->pixels + (size_t)y * frame->pitch;
        for (uint32_t x = 0; x < frame->width;) {
            const uint8_t* p = row + (size_t)x * 4u;
            uint32_t end = x + 1u;
            while (end < frame->width && same_color(p, row + (size_t)end * 4u)) ++end;
            if (++runs > PS2_SHELL_HBX_MAX_RUNS) return HBX_SHELL_GEOMETRY;
            x = end;
        }
    }
    b->receipt.horizontal_runs = runs;
    b->receipt.vertices = runs * 6u;
    b->receipt.frame_before = b->ops.frame(b->renderer);
    b->receipt.frame_after = b->receipt.frame_before;
    b->receipt.renderer_reuse_permitted = 0;
    ++b->receipt.begin_calls;
    b->ops.begin(b->renderer, color(frame->pixels));
    for (uint32_t y = 0; y < frame->height; ++y) {
        const uint8_t* row = frame->pixels + (size_t)y * frame->pitch;
        for (uint32_t x = 0; x < frame->width;) {
            const uint8_t* p = row + (size_t)x * 4u;
            uint32_t end = x + 1u;
            while (end < frame->width && same_color(p, row + (size_t)end * 4u)) ++end;
            b->ops.rect(b->renderer, (float)(x * 2u), (float)(y * 2u),
                        (float)((end - x) * 2u), 2.0f, 0.0f, color(p));
            x = end;
        }
    }
    ++b->receipt.present_calls;
    b->receipt.present_result = b->ops.present(b->renderer);
    b->receipt.frame_after = b->ops.frame(b->renderer);
    if (b->receipt.present_result != HBX_OK) return b->receipt.present_result;
    /* Stock UI returns OK on acquire OUT_OF_DATE without presenting. */
    if (b->receipt.frame_after != b->receipt.frame_before + 1u)
        return HBX_SHELL_UNCONFIRMED;
    b->receipt.present_confirmed = 1;
    b->receipt.renderer_reuse_permitted = 1;
    hbx_frame_end(b->ctx);
    return 0;
}

static int poll(void* context, Ps2ShellInput* out) {
    HbxShellBridge* b = context;
    if (hbx_stop_requested(b->ctx)) {
        b->receipt.stop_during_run = 1;return HBX_SHELL_STOP;
    }
    hbx_input_poll(&b->input_ctx, &b->input);
    if (hbx_stop_requested(b->ctx)) {
        b->receipt.stop_during_run = 1;return HBX_SHELL_STOP;
    }
    out->connected = b->input.connected != 0u;
    out->buttons = (b->input.buttons & HBX_B) ? PS2_SHELL_CIRCLE : 0u;
    /* Preserve neutral arming while any unrelated physical button is held. */
    if (b->input.buttons & ~(uint32_t)HBX_B) out->buttons |= UINT32_C(0x80000000);
    out->event = PS2_SHELL_EVENT_NONE;
    out->reserved = 0;
    return 0;
}
static int wait_input(void* context, uint32_t milliseconds) {
    HbxShellBridge* b = context;
    if (hbx_stop_requested(b->ctx)) {
        b->receipt.stop_during_run = 1;return HBX_SHELL_STOP;
    }
    hbx_sleep_ns((uint64_t)milliseconds * UINT64_C(1000000));
    return 0;
}
static int finish(void* context) {
    HbxShellBridge* b = context;
    /* rect consumed all app-side data synchronously. The UI table provides no
     * idle/release operation with which to recover a failed GPU submission. */
    b->receipt.source_bytes_released = 1;
    if (!b->receipt.begin_calls || b->receipt.present_confirmed) return 0;
    return HBX_SHELL_RETAINED;
}

int ps2_shell_hbx_run_ex(const struct hbx_ctx* ctx, uint8_t* pixels, size_t bytes,
                         Ps2ShellStatus* status, Ps2ShellHbxReceipt* receipt) {
    const uintptr_t c = (uintptr_t)ctx, p = (uintptr_t)pixels;
    const uintptr_t s = (uintptr_t)status, q = (uintptr_t)receipt;
    if (!span(c, sizeof(*ctx)) || c % _Alignof(struct hbx_ctx) ||
        !span(p, bytes) || bytes < PS2_SHELL_HBX_PIXELS_BYTES ||
        !span(s, sizeof(*status)) || s % _Alignof(Ps2ShellStatus) ||
        (q && (!span(q, sizeof(*receipt)) || q % _Alignof(Ps2ShellHbxReceipt))) ||
        !separate(p, bytes, s, sizeof(*status)) ||
        (q && (!separate(p, bytes, q, sizeof(*receipt)) ||
               !separate(s, sizeof(*status), q, sizeof(*receipt)))) ||
        !writable_separate(c, sizeof(*ctx), p, bytes, s, q)) return HBX_ERR_ARG;
    if (status->version != PS2_SHELL_VERSION || status->size != sizeof(*status) ||
        (receipt && (receipt->version != PS2_SHELL_HBX_VERSION ||
                     receipt->size != sizeof(*receipt)))) return HBX_ERR_ARG;
    const int valid = hbx_ctx_check(ctx);
    if (valid != HBX_OK) return valid;
    const uintptr_t u = (uintptr_t)ctx->ui;
    if (!span(u, sizeof(struct hbx_render)) || u % _Alignof(struct hbx_render) ||
        !writable_separate(u, sizeof(struct hbx_render), p, bytes, s, q)) return HBX_ERR_ABI;
    const struct hbx_render* render = (const struct hbx_render*)u;
    if (render->abi != HBX_RENDER_ABI || render->size < sizeof(*render) ||
        !render->self || !render->ops) return HBX_ERR_ABI;
    const uintptr_t o = (uintptr_t)render->ops;
    if (!span(o, sizeof(*render->ops)) || o % _Alignof(struct hbx_ui_ops) ||
        !writable_separate(o, sizeof(*render->ops), p, bytes, s, q) ||
        !span((uintptr_t)render->self, 1) ||
        !writable_separate((uintptr_t)render->self, 1, p, bytes, s, q)) return HBX_ERR_ABI;
    if (!hbx_ui_render(ctx)) return HBX_ERR_ABI;
    struct hbx_ui_ops ops;
    hbx_memcpy(&ops, render->ops, sizeof(ops));
    if (!ops.begin || !ops.rect || !ops.present || !ops.frame ||
        (ctx->flags & HBX_FLAG_NO_AV) ||
        !(ctx->input.pad_handheld || ctx->input.pad_full_key || ctx->input.pad_joy_dual ||
          ctx->input.pad_joy_left || ctx->input.pad_joy_right)) return HBX_ERR_UNSUPPORTED;
    if (ctx->control) {
        const uintptr_t control = (uintptr_t)ctx->control;
        if (!span(control, sizeof(struct hbx_control)) || control % _Alignof(struct hbx_control) ||
            !writable_separate(control, sizeof(struct hbx_control), p, bytes, s, q)) return HBX_ERR_ARG;
    }
    HbxShellBridge b;
    hbx_memset(&b, 0, sizeof(b));
    b.ctx = ctx;b.renderer = render->self;
    hbx_memcpy(&b.input_ctx, ctx, sizeof(b.input_ctx));
    hbx_memcpy(&b.ops, &ops, sizeof(b.ops));
    /* Use only initialized pad callbacks. SDK itself supplies stack scratch;
     * the adapter does not borrow/overwrite caller pad/touch scratch blocks. */
    b.input_ctx.input.pad_state = 0;
    b.input_ctx.input.touch = 0;b.input_ctx.input.touch_state = 0;
    b.receipt.version = PS2_SHELL_HBX_VERSION;b.receipt.size = sizeof(b.receipt);
    b.receipt.caller_renderer_retained = 1;
    b.receipt.source_bytes_released = 1;
    b.receipt.renderer_reuse_permitted = 1;
    b.receipt.geometry_limit_runs = PS2_SHELL_HBX_MAX_RUNS;
    b.receipt.geometry_limit_vertices = PS2_SHELL_HBX_MAX_VERTICES;
    int result;
    if (hbx_stop_requested(ctx)) {
        *status = (Ps2ShellStatus){.version = PS2_SHELL_VERSION, .size = sizeof(*status),
            .result = PS2_SHELL_OK, .lifecycle = PS2_SHELL_NOT_STARTED,
            .exit_reason = PS2_SHELL_EXIT_HOST};
        b.receipt.stop_at_entry = 1;result = HBX_ERR_STOPPED;
    } else {
        const Ps2ShellHost host = {PS2_SHELL_VERSION, sizeof(host), pixels, bytes,
            PS2_SHELL_HBX_PITCH, &b, present, poll, wait_input, finish};
        b.receipt.shell_called = 1;
        const int shell_result = ps2_shell_run(&host, status);
        if (b.receipt.stop_during_run) result = HBX_ERR_STOPPED;
        else if (status->ownership_retained || b.receipt.present_result != HBX_OK ||
                 status->operation_error == HBX_SHELL_UNCONFIRMED) result = HBX_ERR_GPU;
        else result = shell_result == PS2_SHELL_OK ? HBX_OK : HBX_ERR_INTERNAL;
    }
    b.receipt.sdk_result = result;
    if (receipt) hbx_memcpy(receipt, &b.receipt, sizeof(*receipt));
    return result;
}

int ps2_shell_hbx_run(const struct hbx_ctx* ctx, uint8_t* pixels, size_t bytes,
                      Ps2ShellStatus* status) {
    return ps2_shell_hbx_run_ex(ctx, pixels, bytes, status, (Ps2ShellHbxReceipt*)0);
}
