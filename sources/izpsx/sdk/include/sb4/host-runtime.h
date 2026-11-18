#ifndef SB4_HOST_RUNTIME_H
#define SB4_HOST_RUNTIME_H

#include "compat.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*Sb4HostHook)(void);
typedef struct {
    const Sb4HostHook *preinit, *init, *fini;
    size_t preinit_count, init_count, fini_count;
} Sb4HostHooks;

enum { SB4_HOST_RUNTIME_RETAINED = 0xe8f1 };

/* Once per loaded image. These arrays belong to the image's linker script.
 * They must remain live through finalization. Initialization binds the
 * launcher heap before running preinit and init in link order. */
uint32_t sb4HostRuntimeInitialize(const Sb4Compat *compat, const Sb4HostHooks *hooks);

/* Call after the engine has joined its workers and released its services.
 * Drains image-local exit handlers (including C++ global destructors) in
 * reverse registration order, then runs fini in reverse link order.
 * RETAINED means a destructor registration failed. Do not unload the image
 * on that result, including when returned by Initialize. */
uint32_t sb4HostRuntimeFinalize(void);

#ifdef __cplusplus
}
#endif
#endif
