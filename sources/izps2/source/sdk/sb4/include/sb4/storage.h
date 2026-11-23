#ifndef SB4_STORAGE_H
#define SB4_STORAGE_H
#include <stdint.h>
#include <stddef.h>

#define SB4_STORAGE_VERSION 1u
#define SB4_STORAGE_NAME_MAX 769u
#define SB4_STORAGE_LIST_MAX 1024u
#define SB4_STORAGE_PAGE_MAX 8u
#define SB4_STORAGE_LIMIT 0xe658u
#define SB4_STORAGE_INVALID 0xe659u
#define SB4_STORAGE_OPEN_READ 1u
#define SB4_STORAGE_OPEN_UPDATE 3u
#define SB4_STORAGE_WORKER_READ 1u
#define SB4_STORAGE_WORKER_WRITE 2u

struct IzNativeApp;
typedef struct {
    char name[SB4_STORAGE_NAME_MAX];
    uint64_t size;
    uint8_t directory;
} Sb4StorageEntry;

typedef struct {
    uint64_t size;
    uint32_t directory;
} Sb4StorageInfo;
typedef uint32_t (*Sb4StorageStatFunction)(struct IzNativeApp*,const char*,Sb4StorageInfo*);
typedef const char *(*Sb4StorageRelativePath)(struct IzNativeApp*,const char*);

/* Open a module-relative directory; "" means the module directory. Read
 * up to SB4_STORAGE_PAGE_MAX rows per call. A successful empty page is EOF.
 * On failure count is zero and the caller must close the token. */
typedef struct {
    uint32_t (*open)(struct IzNativeApp*,const char*,uint32_t *token);
    uint32_t (*read)(struct IzNativeApp*,uint32_t token,Sb4StorageEntry*,uint32_t capacity,uint32_t *count);
    uint32_t (*close)(struct IzNativeApp*,uint32_t token);
} Sb4StorageDirectories;

/* File tokens belong to the running module and are retired on its exit.
 * Files opened for update support positional writes, resize and flush.
 * Commit persists changes to the SD filesystem. */
typedef struct {
    uint32_t version, bytes;
    uint32_t (*open)(struct IzNativeApp*,const char*,uint32_t mode,uint32_t *token,uint64_t *size);
    uint32_t (*read)(struct IzNativeApp*,uint32_t token,uint64_t offset,void*,uint32_t bytes,uint32_t *actual);
    uint32_t (*write)(struct IzNativeApp*,uint32_t token,uint64_t offset,const void*,uint32_t bytes);
    uint32_t (*size)(struct IzNativeApp*,uint32_t token,uint64_t *size);
    uint32_t (*resize)(struct IzNativeApp*,uint32_t token,uint64_t size);
    uint32_t (*flush)(struct IzNativeApp*,uint32_t token);
    uint32_t (*close)(struct IzNativeApp*,uint32_t token);
    uint32_t (*create)(struct IzNativeApp*,const char*,uint64_t size);
    uint32_t (*mkdir)(struct IzNativeApp*,const char*);
    uint32_t (*commit)(struct IzNativeApp*);
    /* Optional suffix. Rename follows the mounted filesystem's semantics. */
    uint32_t (*rename_file)(struct IzNativeApp*,const char* from,const char* to);
    uint32_t (*remove_file)(struct IzNativeApp*,const char*);
} Sb4StorageFiles;

/* App-relative paths. Launchers that support absolute paths also accept a
 * path under the canonical SD root of this app. An empty path names the
 * module directory. On a failed list, count is zero and entries are unspecified. */
typedef struct {
    uint32_t version, bytes;
    uint32_t (*list)(struct IzNativeApp *app, const char *directory, Sb4StorageEntry *entries,
                     uint32_t capacity, uint32_t *count);
    uint32_t (*file_size)(struct IzNativeApp *app, const char *name, uint64_t *size);
    const Sb4StorageFiles *files;
    uint32_t flags;
    /* Optional. Exact file size or a directory. An empty path names the app root. */
    Sb4StorageStatFunction stat_path;
    const Sb4StorageDirectories *directories;
    /* Optional. Convert a canonical SD path owned by the current app.
     * The return value points into the input string. Empty means the app root. */
    Sb4StorageRelativePath relative_path;
} Sb4Storage;

static inline Sb4StorageRelativePath sb4StoragePathResolver(const Sb4Storage *storage) {
    return storage && storage->version == SB4_STORAGE_VERSION &&
        storage->bytes >= offsetof(Sb4Storage, relative_path) + sizeof(storage->relative_path)
        ? storage->relative_path : NULL;
}

static inline const Sb4StorageDirectories *sb4StorageDirectories(const Sb4Storage *storage) {
    if (!storage || storage->version != SB4_STORAGE_VERSION ||
        storage->bytes < offsetof(Sb4Storage, directories) + sizeof(storage->directories)) return NULL;
    const Sb4StorageDirectories *dirs = storage->directories;
    return dirs && dirs->open && dirs->read && dirs->close ? dirs : NULL;
}

static inline Sb4StorageStatFunction sb4StorageStat(const Sb4Storage *storage) {
    return storage && storage->version == SB4_STORAGE_VERSION &&
        storage->bytes >= offsetof(Sb4Storage, stat_path) + sizeof(storage->stat_path)
        ? storage->stat_path : NULL;
}

static inline const Sb4StorageFiles *sb4StorageFiles(const Sb4Storage *storage) {
    if (!storage || storage->bytes < offsetof(Sb4Storage, flags)) return NULL;
    const Sb4StorageFiles *files = storage->files;
    return files && files->version == SB4_STORAGE_VERSION &&
        files->bytes >= offsetof(Sb4StorageFiles, rename_file) &&
        files->open && files->read && files->write && files->size && files->resize &&
        files->flush && files->close && files->create && files->mkdir && files->commit ? files : NULL;
}

/* The launcher-created engine worker can read or modify module files when
 * the corresponding flag is set. Join it before returning success from STOP.
 * A launcher with managed HostedPosix support can inherit read-only access
 * for threads created by the worker through the HostedPosix wrapper. */
static inline int sb4StorageWorkerHas(const Sb4Storage *storage, uint32_t capability) {
    return capability && storage && storage->bytes >= offsetof(Sb4Storage, flags) + sizeof(storage->flags) &&
        (storage->flags & capability) == capability && sb4StorageFiles(storage);
}

static inline int sb4StorageWorkerRead(const Sb4Storage *storage) {
    return sb4StorageWorkerHas(storage, SB4_STORAGE_WORKER_READ);
}

static inline int sb4StorageWorkerWrite(const Sb4Storage *storage) {
    return sb4StorageWorkerHas(storage, SB4_STORAGE_WORKER_WRITE);
}

static inline const Sb4StorageFiles *sb4StorageFileChanges(const Sb4Storage *storage) {
    const Sb4StorageFiles *files = sb4StorageFiles(storage);
    return files && files->bytes >= sizeof(*files) && files->rename_file && files->remove_file
        ? files : NULL;
}

#ifdef __cplusplus
extern "C" {
#endif
/* Publish a closed staging file over a regular file in the module.
 * Pass normalized module-relative paths. The destination suffix
 * ".iz2s-replace-old" is reserved for recovery on a later call. */
typedef uint32_t (*Sb4StorageReplace)(struct IzNativeApp*,const char *staged,const char *destination);
uint32_t sb4ReplaceFile(struct IzNativeApp*,const char *staged,const char *destination);

/* Launcher-side provider. Call only with a path already scoped by the owner. */
uint32_t sb4SdList(const char *path, Sb4StorageEntry *entries, uint32_t capacity, uint32_t *count);
uint32_t sb4SdFileSize(const char *path, uint64_t *size);
uint32_t sb4SdStat(const char *path, Sb4StorageInfo *info);
#ifdef __cplusplus
}
#endif
#endif
