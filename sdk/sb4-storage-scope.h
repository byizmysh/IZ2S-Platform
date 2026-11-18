#ifndef IZ2S_SB4_STORAGE_SCOPE_H
#define IZ2S_SB4_STORAGE_SCOPE_H

#include <sb4/storage.h>

/* Set only by the launcher after authenticating the installed application.
 * With this flag, relative storage paths start at the SD card itself. */
#define IZ2S_STORAGE_WHOLE_SD (1u << 2)

static inline int iz2sStorageHasWholeSd(const Sb4Storage *storage) {
    return storage && storage->version==SB4_STORAGE_VERSION &&
        storage->bytes>=offsetof(Sb4Storage,flags)+sizeof(storage->flags) &&
        (storage->flags&IZ2S_STORAGE_WHOLE_SD)!=0;
}

#endif
