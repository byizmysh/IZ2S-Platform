#ifndef PS1_FS_DIR_H
#define PS1_FS_DIR_H
#include <stdint.h>
#include <stddef.h>

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
} Ps1SdDirectory;
typedef struct {
 char name[PS1_SD_ENTRY_NAME_MAX];
 uint64_t size;
 uint8_t directory;
} Ps1SdEntry;
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
#endif
