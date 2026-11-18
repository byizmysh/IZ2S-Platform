#ifndef SB4_STORAGE_PROVIDER_H
#define SB4_STORAGE_PROVIDER_H
#include "storage.h"
#include <pthread.h>

enum Sb4StorageScope {
    SB4_STORAGE_NO_SCOPE,
    SB4_STORAGE_ENTRY_SCOPE,
    SB4_STORAGE_WORKER_SCOPE,
    SB4_STORAGE_HOSTED_READ_SCOPE,
};

/* Paths passed by the launcher have already been resolved under the
 * current module directory. The owner is internal and never sent to FS. */
typedef struct {
    uint32_t (*open)(const void*,const char*,uint32_t,uint32_t*,uint64_t*);
    uint32_t (*read)(const void*,uint32_t,uint64_t,void*,uint32_t,uint32_t*);
    uint32_t (*write)(const void*,uint32_t,uint64_t,const void*,uint32_t);
    uint32_t (*size)(const void*,uint32_t,uint64_t*);
    uint32_t (*resize)(const void*,uint32_t,uint64_t);
    uint32_t (*flush)(const void*,uint32_t);
    uint32_t (*close)(const void*,uint32_t);
    uint32_t (*create)(const char*,uint64_t);
    uint32_t (*mkdir)(const char*);
    uint32_t (*commit)(void);
    void (*retire)(const void*);
    uint32_t (*rename_file)(const char*,const char*);
    uint32_t (*remove_file)(const char*);
    void (*set_scope)(const void *owner, enum Sb4StorageScope);
    enum Sb4StorageScope (*get_scope)(const void *owner);
    uint32_t (*stat_path)(const char*,Sb4StorageInfo*);
    uint32_t (*open_dir)(const void*,const char*,uint32_t*);
    uint32_t (*read_dir)(const void*,uint32_t,Sb4StorageEntry*,uint32_t,uint32_t*);
    uint32_t (*close_dir)(const void*,uint32_t);
} Sb4StorageProvider;

const Sb4StorageProvider *sb4SdFiles(void);
/* Launcher-side only. Both paths must already be authorized and normalized. */
uint32_t sb4SdReplacePreparedFile(const char *staged, const char *destination);
uint32_t sb4SdOpenDir(const void *owner, const char *path, uint32_t *token);
uint32_t sb4SdReadDir(const void *owner, uint32_t token, Sb4StorageEntry*, uint32_t capacity, uint32_t *count);
uint32_t sb4SdCloseDir(const void *owner, uint32_t token);
void sb4SdRetireDirs(const void *owner);
int sb4SdCreateHostedThread(pthread_t *thread, const pthread_attr_t *attr,
                           void *(*start)(void *), void *argument);
#endif
