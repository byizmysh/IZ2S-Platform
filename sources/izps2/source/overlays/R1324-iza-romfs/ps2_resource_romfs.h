#ifndef PS2_RESOURCE_ROMFS_H
#define PS2_RESOURCE_ROMFS_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define PS2_ROMFS_MAX_ENTRIES 256u
#define PS2_ROMFS_MAX_PATH 256u
enum { PS2_ROMFS_DIRECTORY=1, PS2_ROMFS_FILE=2 };
typedef struct Ps2ResourceRomfs {
    const unsigned char *blob;
    size_t bytes;
    uint64_t paths_offset, data_offset;
    uint32_t entries, mounted;
} Ps2ResourceRomfs;
typedef struct Ps2ResourceRomfsEntry {
    const char *path;
    const unsigned char *data;
    uint64_t bytes;
    uint32_t kind, index;
} Ps2ResourceRomfsEntry;
/* Caller owns an immutable blob and a zero-initialized descriptor for the
 * entire mount lifetime. No allocation, SDK, syscall or C FILE ABI is used.
 * Mount once outside VM resets; no remount/unmount while descriptors exist. */
int ps2_resource_romfs_mount(Ps2ResourceRomfs *, const void *, size_t);
int ps2_resource_romfs_entry(const Ps2ResourceRomfs *, uint32_t, Ps2ResourceRomfsEntry *);
/* Canonical absolute /resources only. No dot, dotdot, empty components,
 * trailing slash, backslash or controls. Negative results use Linux errno. */
int ps2_resource_romfs_lookup(const Ps2ResourceRomfs *, const char *, Ps2ResourceRomfsEntry *);
int ps2_resource_romfs_read(const Ps2ResourceRomfs *, uint32_t, uint64_t,
                          void *, size_t, size_t *actual);
/* position counts immediate children, not blob byte offsets. Returns 1 child,
 * 0 EOF, or a negative errno. No synthetic '.'/'..' entries (HBFS parity). */
int ps2_resource_romfs_child(const Ps2ResourceRomfs *, uint32_t directory,
                           uint64_t position, Ps2ResourceRomfsEntry *);
#ifdef __cplusplus
}
#endif
#endif
