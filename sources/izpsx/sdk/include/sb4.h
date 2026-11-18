#pragma once
#include <sb4/services.h>
#include <sb4/audio.h>
#include <sb4/compat.h>
#include <sb4/stream.h>
#include <sb4/input.h>
#include <sb4/presentation.h>
#define SB4_SDK_VERSION 1
typedef struct {
    uint32_t version, bytes, services, audioFunctions;
    uint32_t catalogRecords, uniqueSymbols, originalSdkImageLoaded;
    const char *backend;
} Sb4Profile;
#ifdef __cplusplus
extern "C" {
#endif
/* No IPC is opened by these discovery calls. Lifecycle belongs to the caller. */
const Sb4Profile *sb4GetProfile(void);
/* Module 5 offsets identify verified contracts, never synthetic addresses.
 * Unsupported module/offset pairs return NULL. */
IzServiceFunction sb4ResolveService(uint32_t module,uint32_t offset);
#ifdef __cplusplus
}
#endif
