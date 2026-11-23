#include <switch.h>
#include <sb4/storage-provider.h>
#include <stdlib.h>
#include <string.h>

static int name_valid(const char *name, size_t bytes) {
    if (!bytes || (bytes == 1 && name[0] == '.') ||
        (bytes == 2 && name[0] == '.' && name[1] == '.')) return 0;
    for (size_t i = 0; i < bytes; ++i) {
        unsigned ch = (unsigned char)name[i];
        if (ch < 32 || ch == 127 || ch == '/' || ch == '\\' || ch == ':') return 0;
    }
    return 1;
}

static uint32_t copy_entry(Sb4StorageEntry *out, const FsDirectoryEntry *entry) {
    const char *end = memchr(entry->name, '\0', sizeof entry->name);
    size_t bytes = end ? (size_t)(end - entry->name) : sizeof entry->name;
    if (!end || bytes >= sizeof out->name || !name_valid(entry->name, bytes) ||
        (entry->type != FsDirEntryType_Dir && entry->type != FsDirEntryType_File) ||
        (entry->type == FsDirEntryType_File && entry->file_size < 0)) return SB4_STORAGE_INVALID;
    memset(out, 0, sizeof *out);
    memcpy(out->name, entry->name, bytes);
    out->directory = entry->type == FsDirEntryType_Dir;
    out->size = out->directory ? 0 : (uint64_t)entry->file_size;
    return 0;
}

typedef struct OpenDirectory {
    const void *owner;
    FsDir handle;
    uint32_t token, result;
    int eof;
    struct OpenDirectory *next;
} OpenDirectory;
static OpenDirectory *directories;
static uint32_t next_token = 1;
static Mutex dir_mutex;

static OpenDirectory **find_dir_link(const void *owner, uint32_t token) {
    if (!owner || !token) return NULL;
    for (OpenDirectory **link = &directories; *link; link = &(*link)->next)
        if ((*link)->owner == owner && (*link)->token == token) return link;
    return NULL;
}

uint32_t sb4SdOpenDir(const void *owner, const char *path, uint32_t *token) {
    if (token) *token = 0;
    if (!owner || !path || !token) return SB4_STORAGE_INVALID;
    FsFileSystem *sd = fsdevGetDeviceFileSystem("sdmc");
    if (!sd) return SB4_STORAGE_INVALID;
    OpenDirectory *slot = malloc(sizeof *slot);
    if (!slot) return SB4_STORAGE_LIMIT;
    mutexLock(&dir_mutex);
    uint32_t rc = SB4_STORAGE_LIMIT;
    if (next_token < UINT32_C(0x80000000)) {
        rc = fsFsOpenDirectory(sd, path, FsDirOpenMode_ReadDirs | FsDirOpenMode_ReadFiles, &slot->handle);
        if (!rc) {
            slot->owner = owner;
            *token = slot->token = next_token++;
            slot->result = 0;
            slot->eof = 0;
            slot->next = directories;
            directories = slot;
        }
    }
    mutexUnlock(&dir_mutex);
    if (rc) free(slot);
    return rc;
}

uint32_t sb4SdReadDir(const void *owner, uint32_t token, Sb4StorageEntry *entries,
                      uint32_t capacity, uint32_t *count) {
    if (count) *count = 0;
    if (!entries || !count || !capacity || capacity > SB4_STORAGE_PAGE_MAX) return SB4_STORAGE_INVALID;
    mutexLock(&dir_mutex);
    OpenDirectory **link = find_dir_link(owner, token);
    OpenDirectory *slot = link ? *link : NULL;
    uint32_t rc = slot ? slot->result : SB4_STORAGE_INVALID;
    if (!rc && !slot->eof) {
        FsDirectoryEntry page[SB4_STORAGE_PAGE_MAX];
        s64 rows = 0;
        rc = fsDirRead(&slot->handle, &rows, capacity, page);
        if (!rc && (rows < 0 || (uint64_t)rows > capacity)) rc = SB4_STORAGE_INVALID;
        for (s64 i = 0; !rc && i < rows; ++i) rc = copy_entry(&entries[i], &page[i]);
        if (!rc) {
            *count = (uint32_t)rows;
            slot->eof = !rows;
        }
        slot->result = rc;
    }
    mutexUnlock(&dir_mutex);
    return rc;
}

uint32_t sb4SdCloseDir(const void *owner, uint32_t token) {
    mutexLock(&dir_mutex);
    OpenDirectory **link = find_dir_link(owner, token);
    if (!link) {
        mutexUnlock(&dir_mutex);
        return SB4_STORAGE_INVALID;
    }
    OpenDirectory *slot = *link;
    *link = slot->next;
    fsDirClose(&slot->handle);
    mutexUnlock(&dir_mutex);
    free(slot);
    return 0;
}

void sb4SdRetireDirs(const void *owner) {
    if (!owner) return;
    mutexLock(&dir_mutex);
    OpenDirectory **link = &directories;
    while (*link) {
        OpenDirectory *slot = *link;
        if (slot->owner != owner) {
            link = &slot->next;
            continue;
        }
        *link = slot->next;
        fsDirClose(&slot->handle);
        free(slot);
    }
    mutexUnlock(&dir_mutex);
}

uint32_t sb4SdList(const char *path, Sb4StorageEntry *entries, uint32_t capacity, uint32_t *count) {
    if (count) *count = 0;
    if (!path || !entries || !count || !capacity || capacity > SB4_STORAGE_LIST_MAX)
        return SB4_STORAGE_INVALID;
    FsFileSystem *sd = fsdevGetDeviceFileSystem("sdmc");
    if (!sd) return SB4_STORAGE_INVALID;
    FsDir dir;
    uint32_t rc = fsFsOpenDirectory(sd, path, FsDirOpenMode_ReadDirs | FsDirOpenMode_ReadFiles, &dir);
    if (rc) return rc;
    FsDirectoryEntry page[8];
    size_t total = 0;
    for (;;) {
        s64 count_read = 0;
        rc = fsDirRead(&dir, &count_read, sizeof page / sizeof *page, page);
        if (rc || !count_read) break;
        if (count_read < 0 || (uint64_t)count_read > sizeof page / sizeof *page) {
            rc = SB4_STORAGE_INVALID;
            break;
        }
        if ((uint64_t)count_read > capacity - total) {
            rc = SB4_STORAGE_LIMIT;
            break;
        }
        for (s64 i = 0; !rc && i < count_read; ++i) rc = copy_entry(&entries[total++], &page[i]);
        if (rc) break;
    }
    fsDirClose(&dir);
    if (!rc) *count = (uint32_t)total;
    return rc;
}

uint32_t sb4SdFileSize(const char *path, uint64_t *size) {
    if (size) *size = 0;
    if (!path || !size) return SB4_STORAGE_INVALID;
    FsFileSystem *sd = fsdevGetDeviceFileSystem("sdmc");
    if (!sd) return SB4_STORAGE_INVALID;
    FsFile file;
    uint32_t rc = fsFsOpenFile(sd, path, FsOpenMode_Read, &file);
    if (rc) return rc;
    s64 bytes = 0;
    rc = fsFileGetSize(&file, &bytes);
    fsFileClose(&file);
    if (!rc && bytes < 0) rc = SB4_STORAGE_INVALID;
    if (!rc) *size = (uint64_t)bytes;
    return rc;
}

uint32_t sb4SdStat(const char *path, Sb4StorageInfo *info) {
    if (!info) return SB4_STORAGE_INVALID;
    *info = (Sb4StorageInfo){0};
    if (!path) return SB4_STORAGE_INVALID;
    FsFileSystem *sd = fsdevGetDeviceFileSystem("sdmc");
    if (!sd) return SB4_STORAGE_INVALID;
    FsDirEntryType type;
    uint32_t rc = fsFsGetEntryType(sd, path, &type);
    if (rc) return rc;
    if (type == FsDirEntryType_Dir) {
        info->directory = 1;
        return 0;
    }
    return type == FsDirEntryType_File ? sb4SdFileSize(path, &info->size) : SB4_STORAGE_INVALID;
}
