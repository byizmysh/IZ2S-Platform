#include <switch.h>
#include <sb4/storage-provider.h>
#include <sb4/native-app.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

typedef struct OpenFile {
    const void *owner;
    FsFile file;
    uint32_t token;
    uint32_t writable;
    struct OpenFile *next;
} OpenFile;

static OpenFile *files;
static uint32_t next_token = 1;
static Mutex mutex;
static _Thread_local const void *scope_owner;
static _Thread_local enum Sb4StorageScope scope;

static void set_scope(const void *owner, enum Sb4StorageScope value) {
    scope_owner = owner;
    scope = value;
}

static enum Sb4StorageScope get_scope(const void *owner) {
    return owner && owner == scope_owner ? scope : SB4_STORAGE_NO_SCOPE;
}

typedef struct {
    const void *owner;
    void *(*start)(void *);
    void *argument;
} ReadThreadStart;

static void *read_thread_start(void *context) {
    ReadThreadStart entry = *(ReadThreadStart *)context;
    free(context);
    set_scope(entry.owner, SB4_STORAGE_HOSTED_READ_SCOPE);
    void *result = entry.start(entry.argument);
    set_scope(NULL, SB4_STORAGE_NO_SCOPE);
    return result;
}

int sb4SdCreateHostedThread(pthread_t *thread, const pthread_attr_t *attr,
                           void *(*start)(void *), void *argument) {
    if (!thread || !start) return EINVAL;
    if (!scope_owner || (scope != SB4_STORAGE_WORKER_SCOPE && scope != SB4_STORAGE_HOSTED_READ_SCOPE))
        return pthread_create(thread, attr, start, argument);
    ReadThreadStart *entry = malloc(sizeof *entry);
    if (!entry) return ENOMEM;
    *entry = (ReadThreadStart){scope_owner, start, argument};
    int rc = pthread_create(thread, attr, read_thread_start, entry);
    if (rc) free(entry);
    return rc;
}

/* All links and their file handles are protected by mutex. A scoped child
 * cannot acquire a writable token, including to close it. */
static OpenFile **find_link(const void *owner, uint32_t token) {
    if (!owner || !token) return NULL;
    for (OpenFile **link = &files; *link; link = &(*link)->next) {
        OpenFile *slot = *link;
        if (slot->owner == owner && slot->token == token)
            return get_scope(owner) == SB4_STORAGE_HOSTED_READ_SCOPE && slot->writable ? NULL : link;
    }
    return NULL;
}

static OpenFile *find(const void *owner, uint32_t token) {
    OpenFile **link = find_link(owner, token);
    return link ? *link : NULL;
}

static FsFileSystem *sd(void) { return fsdevGetDeviceFileSystem("sdmc"); }

static uint32_t open_file(const void *owner, const char *path, uint32_t mode,
                          uint32_t *token, uint64_t *size) {
    if (token) *token = 0;
    if (size) *size = 0;
    if (!owner || !path || !token || !size ||
        (mode != SB4_STORAGE_OPEN_READ && mode != SB4_STORAGE_OPEN_UPDATE)) return SB4_STORAGE_INVALID;
    uint32_t writable = mode == SB4_STORAGE_OPEN_UPDATE;
    if (writable && get_scope(owner) == SB4_STORAGE_HOSTED_READ_SCOPE)
        return SB4_STORAGE_INVALID;
    // The FS append flag permits file growth. The write still uses its explicit offset.
    uint32_t fs_mode = FsOpenMode_Read;
    if (writable) fs_mode |= FsOpenMode_Write | FsOpenMode_Append;
    FsFileSystem *fs = sd();
    if (!fs) return SB4_STORAGE_INVALID;
    OpenFile *slot = malloc(sizeof *slot);
    if (!slot) return SB4_STORAGE_LIMIT;
    mutexLock(&mutex);
    uint32_t rc = SB4_STORAGE_LIMIT;
    if (next_token < UINT32_C(0x80000000)) {
        rc = fsFsOpenFile(fs, path, fs_mode, &slot->file);
        if (!rc) {
            s64 bytes = 0;
            rc = fsFileGetSize(&slot->file, &bytes);
            if (!rc && bytes < 0) rc = SB4_STORAGE_INVALID;
            if (rc) fsFileClose(&slot->file);
            else {
                slot->owner = owner;
                slot->token = next_token++;
                slot->writable = writable;
                slot->next = files;
                files = slot;
                *token = slot->token;
                *size = (uint64_t)bytes;
            }
        }
    }
    mutexUnlock(&mutex);
    if (rc) free(slot);
    return rc;
}

/* As in libnx fsdev, use ordinary process memory when FS rejects the
 * caller's buffer for direct I/O. Keep offsets and the handle unchanged. */
static uint32_t read_buffered(FsFile *file, s64 offset, uint8_t *data, uint32_t bytes,
                              uint64_t *read) {
    uint8_t buffer[4096];
    *read = 0;
    while (*read < bytes) {
        uint32_t remaining = bytes - (uint32_t)*read;
        uint32_t chunk = remaining < sizeof buffer ? remaining : sizeof buffer;
        uint64_t count = 0;
        uint32_t rc = fsFileRead(file, offset + (s64)*read, buffer, chunk, FsReadOption_None, &count);
        if (rc) return rc;
        if (count > chunk) return SB4_STORAGE_INVALID;
        memcpy(data + *read, buffer, (size_t)count);
        *read += count;
        if (count < chunk) break;
    }
    return 0;
}

static uint32_t write_buffered(FsFile *file, s64 offset, const uint8_t *data, uint32_t bytes) {
    uint8_t buffer[4096];
    while (bytes) {
        uint32_t chunk = bytes < sizeof buffer ? bytes : sizeof buffer;
        memcpy(buffer, data, chunk);
        uint32_t rc = fsFileWrite(file, offset, buffer, chunk, FsWriteOption_None);
        if (rc) return rc;
        data += chunk;
        offset += chunk;
        bytes -= chunk;
    }
    return 0;
}

static uint32_t read_file_at(const void *owner, uint32_t token, uint64_t offset,
                             void *data, uint32_t bytes, uint32_t *actual) {
    if (actual) *actual = 0;
    if (!actual || (!data && bytes) || offset > INT64_MAX || bytes > INT64_MAX - offset)
        return SB4_STORAGE_INVALID;
    mutexLock(&mutex);
    OpenFile *slot = find(owner, token);
    uint64_t read = 0;
    uint32_t rc = slot ? 0 : SB4_STORAGE_INVALID;
    if (!rc && bytes) {
        rc = fsFileRead(&slot->file, (s64)offset, data, bytes, FsReadOption_None, &read);
        if (R_VALUE(rc) == KERNELRESULT(InvalidMemoryState))
            rc = read_buffered(&slot->file, (s64)offset, data, bytes, &read);
    }
    if (!rc && read > bytes) rc = SB4_STORAGE_INVALID;
    if (!rc) *actual = (uint32_t)read;
    mutexUnlock(&mutex);
    return rc;
}

static uint32_t write_file_at(const void *owner, uint32_t token, uint64_t offset,
                              const void *data, uint32_t bytes) {
    if ((!data && bytes) || offset > INT64_MAX || bytes > INT64_MAX - offset)
        return SB4_STORAGE_INVALID;
    mutexLock(&mutex);
    OpenFile *slot = find(owner, token);
    uint32_t rc = slot && slot->writable ? 0 : SB4_STORAGE_INVALID;
    if (!rc && bytes) {
        rc = fsFileWrite(&slot->file, (s64)offset, data, bytes, FsWriteOption_None);
        if (R_VALUE(rc) == KERNELRESULT(InvalidMemoryState))
            rc = write_buffered(&slot->file, (s64)offset, data, bytes);
    }
    mutexUnlock(&mutex);
    return rc;
}

static uint32_t file_size(const void *owner, uint32_t token, uint64_t *size) {
    if (!size) return SB4_STORAGE_INVALID;
    *size = 0;
    mutexLock(&mutex);
    OpenFile *slot = find(owner, token);
    s64 bytes = 0;
    uint32_t rc = slot ? fsFileGetSize(&slot->file, &bytes) : SB4_STORAGE_INVALID;
    if (!rc && bytes < 0) rc = SB4_STORAGE_INVALID;
    if (!rc) *size = (uint64_t)bytes;
    mutexUnlock(&mutex);
    return rc;
}

static uint32_t resize_file(const void *owner, uint32_t token, uint64_t size) {
    if (size > INT64_MAX) return SB4_STORAGE_INVALID;
    mutexLock(&mutex);
    OpenFile *slot = find(owner, token);
    uint32_t rc = slot && slot->writable ? fsFileSetSize(&slot->file, (s64)size) : SB4_STORAGE_INVALID;
    mutexUnlock(&mutex);
    return rc;
}

static uint32_t flush_file(const void *owner, uint32_t token) {
    mutexLock(&mutex);
    OpenFile *slot = find(owner, token);
    uint32_t rc = slot && slot->writable ? fsFileFlush(&slot->file) : SB4_STORAGE_INVALID;
    mutexUnlock(&mutex);
    return rc;
}

static uint32_t close_file(const void *owner, uint32_t token) {
    mutexLock(&mutex);
    OpenFile **link = find_link(owner, token);
    if (!link) {
        mutexUnlock(&mutex);
        return SB4_STORAGE_INVALID;
    }
    OpenFile *slot = *link;
    *link = slot->next;
    fsFileClose(&slot->file);
    mutexUnlock(&mutex);
    free(slot);
    return 0;
}

static uint32_t create_file(const char *path, uint64_t size) {
    FsFileSystem *fs = sd();
    if (!fs || !path || size > INT64_MAX) return SB4_STORAGE_INVALID;
    mutexLock(&mutex);
    uint32_t rc = fsFsCreateFile(fs, path, (s64)size, 0);
    mutexUnlock(&mutex);
    return rc;
}
static uint32_t make_dir(const char *path) {
    FsFileSystem *fs = sd();
    if (!fs || !path) return SB4_STORAGE_INVALID;
    mutexLock(&mutex);
    uint32_t rc = fsFsCreateDirectory(fs, path);
    mutexUnlock(&mutex);
    return rc;
}
static uint32_t commit(void) {
    FsFileSystem *fs = sd();
    if (!fs) return SB4_STORAGE_INVALID;
    mutexLock(&mutex);
    uint32_t rc = fsFsCommit(fs);
    mutexUnlock(&mutex);
    return rc;
}
static uint32_t rename_file(const char *from, const char *to) {
    FsFileSystem *fs = sd();
    if (!fs || !from || !to) return SB4_STORAGE_INVALID;
    mutexLock(&mutex);
    uint32_t rc = fsFsRenameFile(fs, from, to);
    mutexUnlock(&mutex);
    return rc;
}
static uint32_t remove_file(const char *path) {
    FsFileSystem *fs = sd();
    if (!fs || !path) return SB4_STORAGE_INVALID;
    mutexLock(&mutex);
    uint32_t rc = fsFsDeleteFile(fs, path);
    mutexUnlock(&mutex);
    return rc;
}
static void retire(const void *owner) {
    if (!owner) return;
    mutexLock(&mutex);
    OpenFile **link = &files;
    while (*link) {
        OpenFile *slot = *link;
        if (slot->owner != owner) {
            link = &slot->next;
            continue;
        }
        *link = slot->next;
        fsFileClose(&slot->file);
        free(slot);
    }
    mutexUnlock(&mutex);
    sb4SdRetireDirs(owner);
}

const Sb4StorageProvider *sb4SdFiles(void) {
    static const Sb4StorageProvider provider = {
        open_file, read_file_at, write_file_at, file_size, resize_file, flush_file, close_file,
        create_file, make_dir, commit, retire, rename_file, remove_file, set_scope, get_scope, sb4SdStat,
        sb4SdOpenDir, sb4SdReadDir, sb4SdCloseDir,
    };
    return &provider;
}

static uint32_t regular_file(Sb4StorageStatFunction stat_path, IzNativeApp *app,
                             const char *path, int *exists) {
    enum { PathNotFound = 0x202 };
    Sb4StorageInfo info;
    const uint32_t result = stat_path(app, path, &info);
    *exists = result == 0;
    if (result == PathNotFound) return 0;
    return result ? result : info.directory ? SB4_STORAGE_INVALID : 0;
}

/* Run with the replace lock held. Keep the previous file until the new
 * destination is committed, and recognize it on retry after interruption. */
static uint32_t replace(IzNativeApp *app, const Sb4StorageFiles *files,
                        Sb4StorageStatFunction stat_path, const char *staged,
                        const char *destination, const char *previous) {
    int source, target, backup;
    uint32_t rc = regular_file(stat_path, app, staged, &source);
    if (!rc && !source) return 0x202;
    if (!rc) rc = regular_file(stat_path, app, destination, &target);
    if (!rc) rc = regular_file(stat_path, app, previous, &backup);
    if (rc) return rc;

    if (target && backup) {
        rc = files->commit(app);
        if (!rc) rc = files->remove_file(app, previous);
        if (rc) return rc;
        backup = 0;
    }
    if (target) {
        rc = files->rename_file(app, destination, previous);
        if (rc) return rc;
        backup = 1;
        rc = files->commit(app);
    }
    if (!rc) rc = files->rename_file(app, staged, destination);
    if (rc) {
        if (backup && !files->rename_file(app, previous, destination))
            (void)files->commit(app);
        return rc;
    }
    rc = files->commit(app);
    if (!rc && backup && !files->remove_file(app, previous))
        (void)files->commit(app);
    return rc;
}

static uint32_t replace_paths(IzNativeApp *app, const Sb4StorageFiles *files,
                              Sb4StorageStatFunction stat_path, const char *staged,
                              const char *destination) {
    static Mutex replace_lock;
    static const char suffix[] = ".iz2s-replace-old";
    if (!files || !stat_path || !staged || !*staged || !destination || !*destination)
        return SB4_STORAGE_INVALID;
    char previous[SB4_STORAGE_NAME_MAX];
    const size_t length = strlen(destination);
    if (length > sizeof(previous) - sizeof(suffix) || !strcmp(staged, destination))
        return SB4_STORAGE_INVALID;
    memcpy(previous, destination, length);
    memcpy(previous + length, suffix, sizeof(suffix));
    if (!strcmp(staged, previous)) return SB4_STORAGE_INVALID;

    mutexLock(&replace_lock);
    const uint32_t rc = replace(app, files, stat_path, staged, destination, previous);
    mutexUnlock(&replace_lock);
    return rc;
}

uint32_t sb4ReplaceFile(IzNativeApp *app, const char *staged, const char *destination) {
    const Sb4Storage *storage = sb4ModuleStorage(app);
    return replace_paths(app, sb4StorageFileChanges(storage), sb4StorageStat(storage), staged, destination);
}

static uint32_t stat_prepared(IzNativeApp *app, const char *path, Sb4StorageInfo *info) {
    (void)app;
    return sb4SdStat(path, info);
}

static uint32_t rename_prepared(IzNativeApp *app, const char *from, const char *to) {
    (void)app;
    return rename_file(from, to);
}

static uint32_t remove_prepared(IzNativeApp *app, const char *path) {
    (void)app;
    return remove_file(path);
}

static uint32_t commit_prepared(IzNativeApp *app) {
    (void)app;
    return commit();
}

uint32_t sb4SdReplacePreparedFile(const char *staged, const char *destination) {
    static const Sb4StorageFiles ops = {
        .rename_file = rename_prepared,
        .remove_file = remove_prepared,
        .commit = commit_prepared,
    };
    return replace_paths(NULL, &ops, stat_prepared, staged, destination);
}
