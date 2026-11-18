#include "bridge.h"
#include "services.h"
#include <string.h>

extern uint32_t izpsx_engine(IzNativeApp *, uint32_t);
extern uint32_t izpsx_release_stack(IzNativeApp *);
extern const unsigned char izpsx_checked_shim[], izpsx_checked_shim_end[];

uintptr_t ps1_imports[PS1_IMPORT_COUNT];

static uint32_t stop(IzNativeApp *app, IzpsxBridge *bridge) {
    if (!bridge->initialized) return 0;
    uint32_t result = izpsx_engine(app, IZ_NATIVE_STOP);
    if (result) return result;
    result = izpsx_release_stack(app);
    if (!result) bridge->initialized = 0;
    return result;
}

static uint32_t run(IzNativeApp *app, uint32_t event) {
    if (!ps1_sd_public_app(app)) return 0xf210;
    IzpsxBridge *bridge = app->data;
    if (event == IZ_NATIVE_STOP) return stop(app, bridge);
    if (event == IZ_NATIVE_INIT || bridge->reload) {
        uint32_t result = stop(app, bridge);
        if (result) return result;
        bridge->reload = 0;
        bridge->initialized = 1;
        app->flags |= IZ_NATIVE_MANAGED_INPUT;
        return izpsx_engine(app, IZ_NATIVE_INIT);
    }
    return izpsx_engine(app, event);
}

typedef uint32_t (*NativeEntry)(IzNativeApp *, uint32_t);

NativeEntry izpsx_select_prelinked(uint32_t abi, const char *id, const void *code, uint32_t bytes) {
    const size_t expected = (uintptr_t)izpsx_checked_shim_end - (uintptr_t)izpsx_checked_shim;
    if (abi != SB4_NATIVE_ABI || strcmp(id, "izpsx") || bytes != expected ||
        memcmp(code, izpsx_checked_shim, bytes)) return NULL;
    return run;
}

