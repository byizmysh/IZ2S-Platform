#include <sb4/native-app.h>

extern const char *const store_import_names[];
extern Sb4CFunction store_imports[];
extern const unsigned store_import_count;
extern uint32_t iz_store_iza_event(IzNativeApp *, uint32_t);

__attribute__((section(".text.store_engine")))
uint32_t store_engine(IzNativeApp *app, uint32_t event) {
    if (event == IZ_NATIVE_INIT) {
        const Sb4Compat *api = sb4ModuleCompat(app);
        if (!api) return 0xe870;
        /* Resolve each required service independently of the runtime release. */
        for (unsigned i = 0; i < store_import_count; ++i) {
            store_imports[i] = api->resolve_c(store_import_names[i]);
            if (!store_imports[i]) {
                unsigned bytes = 0;
                while (store_import_names[i][bytes]) ++bytes;
                if (app->write_file) app->write_file(app, "missing-sdk-import.txt", store_import_names[i], bytes);
                return 0xe872;
            }
        }
    }
    return iz_store_iza_event(app, event);
}
