#include <sb4/native-app.h>
#include <sb4/native-services.h>
#include <string.h>

extern uint32_t iz_store_iza_event(IzNativeApp *, uint32_t);
static const Sb4Compat *launcher_compat;
static Sb4Compat store_compat;

static Sb4CFunction store_resolve(const char *name) {
    if (!name) return NULL;
    if (!strcmp(name, "sb4IZ2SCheeseEvent")) return (Sb4CFunction)iz_store_iza_event;
    return launcher_compat->resolve_c(name);
}

/* Legacy Store packages call the embedded handler. Independent Store images
 * resolve the same system imports and keep executing their own application. */
const Sb4Compat *iz_ounce_store_compat(const Sb4Compat *base) {
    if (!sb4CompatValid(base) || base->bytes < sizeof store_compat) return base;
    launcher_compat = sb4GetApplicationIoCompat(base);
    store_compat = *launcher_compat;
    store_compat.resolve_c = store_resolve;
    return &store_compat;
}
