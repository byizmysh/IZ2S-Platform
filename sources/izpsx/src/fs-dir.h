#ifndef PS1_FS_DIR_H
#define PS1_FS_DIR_H
#include <stdint.h>
#include <stddef.h>
#ifdef IZPSX_SB4_STORAGE
#include <sb4/storage.h>
#endif

#define PS1_SD_PATH_MAX 769u
#define PS1_SD_ENTRY_NAME_MAX 769u
typedef uint32_t (*Ps1SdSend)(void *, uint64_t, uint32_t);
typedef uint32_t (*Ps1SdClose)(uint32_t);
typedef struct {
 Ps1SdSend send;
 Ps1SdClose close_handle;
 uint32_t filesystem; /* Borrowed promoted SD IFileSystem; never closed here. */
 void *buffer;       /* Private, 4096-aligned, at least 0x310 bytes. */
 size_t capacity;    /* At most 65536 bytes. */
 uint32_t error, transport, leaked_handle;
 uint64_t calls, entries_read;
#ifdef IZPSX_SB4_STORAGE
 struct IzNativeApp *app;
#endif
} Ps1SdDirectory;
#ifdef IZPSX_SB4_STORAGE
typedef Sb4StorageEntry Ps1SdEntry;
#else
typedef struct {
 char name[PS1_SD_ENTRY_NAME_MAX];
 uint64_t size;
 uint8_t directory;
} Ps1SdEntry;
#endif
enum {PS1_SD_INVALID=0xf001, PS1_SD_PACKET=0xf002, PS1_SD_LIMIT=0xf003,
      PS1_SD_ENTRY=0xf004, PS1_SD_CLOSE=0xf005};

/* Strict path operations reject traversal and malformed directory names.
 * Paths are relative to the borrowed filesystem, e.g. /izmysh/ps1/game.cue. */
int ps1_sd_path_valid(const char *root, const char *path);
int ps1_sd_join(const char *root, const char *directory, const char *name,
                char out[PS1_SD_PATH_MAX]);
int ps1_sd_parent(const char *root, const char *path,
                  char out[PS1_SD_PATH_MAX]);
/* All entries are read before filtering. Capacity exhaustion is an error;
 * the UI never presents a silently incomplete directory. */
uint32_t ps1_sd_list(Ps1SdDirectory *, const char *root, const char *path,
                     Ps1SdEntry *, size_t capacity, size_t *count);
/* Sort folders first, filter supported images. BINs are omitted when this
 * directory contains CUE files, which prevents duplicate multi-track entries. */
size_t ps1_sd_filter_images(Ps1SdEntry *, size_t count);
#ifdef IZPSX_SB4_STORAGE
typedef struct {
 Ps1SdEntry *entries;
 size_t capacity;
} Ps1SdCatalog;
/* The catalog uses the launcher allocator so game core resets cannot reclaim it.
 * Successful loads return a complete, unsorted list of candidate games and folders. */
uint32_t ps1_sd_catalog_load(Ps1SdDirectory *, const char *root, const char *path,
                            Ps1SdCatalog *, size_t *count);
void ps1_sd_catalog_release(Ps1SdDirectory *, Ps1SdCatalog *);
#endif
#endif
