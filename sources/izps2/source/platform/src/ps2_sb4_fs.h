#ifndef PS2_SB4_FS_H
#define PS2_SB4_FS_H

#include "sdk_fs.h"
#include <sb4/storage.h>

struct IzNativeApp;

#ifdef __cplusplus
extern "C" {
#endif

/* Bind before starting file users and unbind after they have closed their
 * files. One binding per IZPS2 launch. */
const Ps2SdkFsApi *ps2_sb4_fs_bind(struct IzNativeApp *app);
uint32_t ps2_sb4_fs_unbind(void);
uint32_t ps2_sb4_fs_path_size(const char *path, uint64_t *bytes);
uint32_t ps2_sb4_fs_stat(const char *path, Sb4StorageInfo *info);
uint32_t ps2_sb4_fs_list(const char *path, Sb4StorageEntry *entries, uint32_t capacity, uint32_t *count);
uint32_t ps2_sb4_fs_open_dir(const char *path, uint32_t *token);
uint32_t ps2_sb4_fs_read_dir(uint32_t token, Sb4StorageEntry *entries, uint32_t capacity, uint32_t *count);
uint32_t ps2_sb4_fs_close_dir(uint32_t token);
uint32_t ps2_sb4_fs_ensure_directory(const char *name);
uint32_t ps2_sb4_fs_remove_save(const char *path);
uint32_t ps2_sb4_fs_commit_saves(void);
int ps2_sb4_fs_is_not_found(uint32_t result);
uint32_t ps2_sb4_fs_rename_save(const char *from, const char *to);
uint32_t ps2_sb4_fs_replace_cache(const char *from, const char *to);
#ifdef IZPS2_WRAPPER_AUDIT
typedef struct {
    uint64_t calls, bytes, ticks, errors;
} Ps2Sb4ReadTiming;
Ps2Sb4ReadTiming ps2_sb4_fs_read_timing(void);
#endif

#ifdef __cplusplus
}
#endif
#endif
