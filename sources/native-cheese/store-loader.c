#include "abi.h"
#include <string.h>

extern const Sb4Compat *iz_ounce_store_compat(const Sb4Compat *);
extern u32 __real_iz_native_load_saved(IzFs *, const IzAppHeader *, const u8 *,
                                     IzNative *, void *, u64, const void *);

/* The R1038 family reuses the R949 loader. Apply the same per-app service
 * table as the source launcher without replacing its prelinked menu objects. */
u32 __wrap_iz_native_load_saved(IzFs *fs, const IzAppHeader *header, const u8 *image,
                              IzNative *native, void *memory, u64 bytes, const void *saved) {
    const Sb4Compat *base = fs->c->sb4_compat;
    if ((header->flags & IZ_APP_SB4_COMPAT) &&
        !memcmp(header->id, "iz2s-cheese", sizeof "iz2s-cheese"))
        fs->c->sb4_compat = iz_ounce_store_compat(base);
    u32 result = __real_iz_native_load_saved(fs, header, image, native, memory, bytes, saved);
    fs->c->sb4_compat = base;
    return result;
}
