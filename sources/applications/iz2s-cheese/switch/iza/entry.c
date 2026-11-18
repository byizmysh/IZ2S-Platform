#include <sb4/native-app.h>
#include "engine-asset.h"

typedef uint32_t (*StoreEvent)(IzNativeApp *, uint32_t);
typedef struct { IzNativeImage image; uint32_t started; } State;

__attribute__((section(".text.iz_app")))
uint32_t iz_app(IzNativeApp *app, uint32_t event) {
    const Sb4Compat *services = sb4ModuleCompat(app);
    if (!services || !app->data || app->data_bytes < sizeof(State))
        return event == IZ_NATIVE_STOP ? 0 : 0xe870;
    State *state = app->data;
    IzNativeApp3 *loader = (IzNativeApp3 *)app;
    if (event == IZ_NATIVE_INIT) {
        if (!loader->load_image || !loader->unload_image) return 0xe873;
        uint32_t rc = loader->load_image(app, STORE_ENGINE_ASSET, &state->image);
        if (rc) return rc;
        if (state->image.version != 1 || state->image.bytes < sizeof state->image || !state->image.entry)
            return 0xe874;
        rc = ((StoreEvent)state->image.entry)(app, event);
        if (!rc) state->started = 1;
        return rc;
    }
    if (event == IZ_NATIVE_STOP) {
        if (state->started) {
            uint32_t rc = ((StoreEvent)state->image.entry)(app, event);
            if (rc) return rc;
            state->started = 0;
        }
        if (state->image.token) {
            uint32_t rc = loader->unload_image(app, state->image.token);
            if (rc) return rc;
            state->image.token = 0;
        }
        return 0;
    }
    return state->started ? ((StoreEvent)state->image.entry)(app, event) : 0xe875;
}
