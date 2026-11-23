#ifndef SB4_JIT_CACHE_H
#define SB4_JIT_CACHE_H

#include <stddef.h>
#include <stdint.h>

#define SB4_JIT_CACHE_RESOLVER "sb4GetJitCacheApi"

typedef struct {
    void *owner;
    uint8_t *rw;
    uint8_t *rx;
} Sb4JitCache;

/* Optional native host service resolved through Sb4Compat::resolve_c.
 * Calls return the SDK Result. The client owns one cache per CPU session.
 * Before publishing, the client must exclude other threads from the updated
 * code. Publish synchronizes instruction fetch on the calling thread.
 * If open or close fails and owner is still set, the client retains ownership.
 */
typedef struct Sb4JitCacheApi {
    uint32_t (*open)(Sb4JitCache *cache, size_t bytes);
    uint32_t (*publish)(const Sb4JitCache *cache, size_t offset, size_t bytes);
    uint32_t (*close)(Sb4JitCache *cache);
} Sb4JitCacheApi;

typedef const Sb4JitCacheApi *(*Sb4JitCacheResolver)(void);

#endif
