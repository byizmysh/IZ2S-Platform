#include <sb4/native-app.h>

extern const char *const store_import_names[];
extern Sb4CFunction store_imports[];
extern const unsigned store_import_count;
extern uint32_t iz_store_iza_event(IzNativeApp *, uint32_t);

static int import_is(const char *actual, const char *expected) {
    while (*actual && *actual == *expected) { ++actual; ++expected; }
    return *actual == *expected;
}

/* Keep the import in the application manifest for launchers that export it.
 * Earlier service tables omit this libc diagnostic; a failed assertion is
 * still fatal, but its absence must not reject an otherwise usable app. */
__attribute__((noreturn)) static void store_assert_fallback(
    const char *file, int line, const char *function, const char *expression) {
    (void)file; (void)line; (void)function; (void)expression;
    __builtin_trap();
}

__attribute__((section(".text.store_engine")))
uint32_t store_engine(IzNativeApp *app, uint32_t event) {
    if (event == IZ_NATIVE_INIT) {
        const Sb4Compat *api = sb4ModuleCompat(app);
        if (!api) return 0xe870;
        /* Resolve each required service independently of the runtime release. */
        for (unsigned i = 0; i < store_import_count; ++i) {
            store_imports[i] = api->resolve_c ? api->resolve_c(store_import_names[i]) : 0;
            if (!store_imports[i] && import_is(store_import_names[i], "__assert_func"))
                store_imports[i] = (Sb4CFunction)store_assert_fallback;
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
