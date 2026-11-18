#ifndef SB4_COMPAT_H
#define SB4_COMPAT_H

#include "services.h"

#define SB4_COMPAT_VERSION 1u
#define SB4_COMPAT_LIBOUNCE 1u

/* Borrowed for the lifetime of an IZlauncher application. An import is a
 * function pointer: cast it to the documented C prototype before calling.
 * Unknown names and unimplemented SB4 service contracts return NULL. */
typedef void (*Sb4CFunction)(void);
typedef struct {
    uint32_t version, bytes, backend, reserved;
    Sb4CFunction (*resolve_c)(const char *name);
    IzServiceFunction (*resolve_service)(uint32_t module, uint32_t offset);
    uint32_t (*allocate_aligned)(void **out, size_t alignment, size_t bytes);
    void (*release)(void *allocation);
    uint64_t (*ticks)(void);
    uint64_t (*tick_hz)(void);
    void (*sleep_ns)(uint64_t ns);
    /* Optional suffix supplied by a launcher with a Vulkan device. */
    Sb4CFunction (*resolve_vulkan)(const char *name);
} Sb4Compat;

static inline int sb4CompatValid(const Sb4Compat *api) {
    return api && api->version == SB4_COMPAT_VERSION && api->bytes >= offsetof(Sb4Compat, resolve_vulkan) &&
           api->backend == SB4_COMPAT_LIBOUNCE && !api->reserved &&
           api->resolve_c && api->resolve_service && api->allocate_aligned &&
           api->release && api->ticks && api->tick_hz && api->sleep_ns;
}

#ifdef __cplusplus
extern "C" {
#endif
const Sb4Compat *sb4GetCompat(void);
#ifdef __cplusplus
}
#endif
#endif
