#include <sb4/native-app.h>
#include <sb4/stream.h>

typedef struct {
    uint32_t source;
    uint64_t token;
    Sb4StreamOpenPC open;
    Sb4StreamUpdatePC update;
    Sb4StreamClosePC close;
} MediaState;

enum {
    StreamOffset = 0x700c80, ClockOffset = StreamOffset + 60 * sizeof(uint32_t),
    RestartOffset = StreamOffset + 107 * sizeof(uint32_t), ShaderClockHz = 31250000
};

static void update_clock(IzNativeApp *app, const Sb4Compat *compat) {
    const uint64_t frequency = compat->tick_hz();
    if (!frequency || frequency > UINT64_MAX / ShaderClockHz) return;
    const uint64_t ticks = compat->ticks();
    const uint64_t converted = (ticks / frequency) * ShaderClockHz +
        (ticks % frequency) * ShaderClockHz / frequency;
    uint32_t *clock = (void *)((unsigned char *)app->controls + ClockOffset);
    clock[0] = (uint32_t)converted;
    clock[1] = (uint32_t)(converted >> 32);
    clock[2] = ShaderClockHz;
}

__attribute__((section(".text.iz_app")))
uint32_t iz_app(IzNativeApp *app, uint32_t event) {
    const Sb4Compat *compat = sb4ModuleCompat(app);
    if (!compat || !app->data || app->data_bytes < sizeof(MediaState) || !app->controls ||
        app->controls_bytes < RestartOffset + sizeof(uint32_t)) return SB4_STREAM_INVALID;
    MediaState *state = app->data;
    if (event == IZ_NATIVE_STOP) {
        const uint32_t rc = state->token ? state->close(app, state->token) : 0;
        if (rc != IZ_NATIVE_STOP_PENDING) state->token = 0;
        return rc;
    }
    if (event == IZ_NATIVE_INIT) {
        app->flags |= IZ_NATIVE_MANAGED_INPUT;
        state->token = 0;
        state->open = (Sb4StreamOpenPC)compat->resolve_c("sb4StreamOpenPC");
        state->update = (Sb4StreamUpdatePC)compat->resolve_c("sb4StreamUpdatePC");
        state->close = (Sb4StreamClosePC)compat->resolve_c("sb4StreamClosePC");
        if (!state->open || !state->update || !state->close) return SB4_STREAM_INVALID;
        if (state->source != SB4_STREAM_BLITBUN && state->source != SB4_STREAM_VACOOTUBE)
            return SB4_STREAM_INVALID;
    }
    update_clock(app, compat);
    uint32_t *restart = (void *)((unsigned char *)app->controls + RestartOffset);
    if (*restart) {
        const uint32_t rc = state->token ? state->close(app,state->token) : 0;
        if (rc == IZ_NATIVE_STOP_PENDING) return 0;
        state->token = 0;
        *restart = 0;
    }
    if (!state->token) {
        if (event == IZ_NATIVE_FRAME && app->frames % 60) return 0;
        const uint32_t rc = state->open(state->source, &state->token);
        if (rc) return rc == SB4_STREAM_UNAVAILABLE ? 0 : rc;
    }
    const uint32_t rc = state->update(app, state->token);
    return rc == SB4_STREAM_PENDING || rc == SB4_STREAM_UNAVAILABLE ? 0 : rc;
}
