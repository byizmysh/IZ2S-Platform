#ifndef SB4_HOST_POSIX_H
#define SB4_HOST_POSIX_H

#include "compat.h"

#define SB4_HOST_POSIX_VERSION 1u

/* Query the launcher contract from a frontend before loading a hosted engine. */
static inline uint32_t sb4HostPosixVersion(const Sb4Compat *compat) {
    if (!sb4CompatValid(compat)) return 0;
    uint32_t (*version)(void) = (uint32_t (*)(void))compat->resolve_c("sb4HostPosixVersion");
    return version ? version() : 0;
}

#ifdef __cplusplus
extern "C" {
#endif

/* Bind before running image constructors or starting threads. The provider
 * owns the POSIX runtime through teardown. Additional imports are optional
 * and report ENOSYS individually when unavailable. */
uint32_t sb4HostPosixBind(const Sb4Compat *compat);

/* Threads created from a module storage worker inherit its read-only scope. */
int sb4HostPosixCanReadStorageFromThreads(void);

/* Close admission and finish joining any detached threads whose exit can be
 * observed. EBUSY keeps the image and its resources owned until every thread
 * has acknowledged join. Older providers may be unable to reap a detached thread. */
uint32_t sb4HostPosixQuiesce(void);

#ifdef __cplusplus
}
#endif

#endif
