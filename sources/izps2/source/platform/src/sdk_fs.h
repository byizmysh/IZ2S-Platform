#ifndef PS2_SDK_FS_H
#define PS2_SDK_FS_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* The SDK only receives its own 8-byte FileHandle, buffers and scalar values.
 * enter saves an 8-byte ScopedAutoAbortDisabler; restore receives that saved
 * FsContext pointer by value. Resolve these functions from the active SDK. */
typedef struct {
    void (*enter)(uint64_t *previous);
    void (*restore)(uint64_t previous);
    uint32_t (*open)(uint64_t *,const char *,int);
    void (*close)(uint64_t);
    uint32_t (*get_size)(int64_t *,uint64_t);
    uint32_t (*read)(size_t *,uint64_t,int64_t,void *,size_t);
    uint32_t (*write)(uint64_t,int64_t,const void *,size_t,const uint32_t *option);
    uint32_t (*flush)(uint64_t);
    uint32_t (*set_size)(uint64_t,int64_t);
    uint32_t (*create)(const char *,int64_t,int);
    int (*is_not_found)(uint32_t);
} Ps2SdkFsApi;
typedef struct {
    Ps2SdkFsApi api;
    uint64_t calls,opened,closed,read_bytes,written_bytes;
    uint32_t ready;
} Ps2SdkFs;
typedef struct {
    Ps2SdkFs *owner;
    uint64_t handle,size;
    uint32_t opened,writable,dirty;
} Ps2SdkFile;
enum {PS2_FS_INVALID=0xf201,PS2_FS_UNSUPPORTED=0xf202,PS2_FS_BAD_REPLY=0xf203,PS2_FS_READ_ONLY=0xf204};
enum {PS2_FS_READ=1,PS2_FS_WRITE=2,PS2_FS_UPDATE_EXISTING=4};
int ps2_sdk_fs_initialize(Ps2SdkFs *,const Ps2SdkFsApi *);
/* out must be zero-initialized. One owner serializes operations on each file;
 * different files may be used concurrently. SDK abort policy is scoped per
 * calling thread for every operation, including failure cleanup. */
uint32_t ps2_sdk_fs_open(Ps2SdkFs *,const char *,unsigned access,Ps2SdkFile *out);
uint32_t ps2_sdk_fs_read(Ps2SdkFile *,uint64_t,void *,size_t,size_t *actual);
uint32_t ps2_sdk_fs_write(Ps2SdkFile *,uint64_t,const void *,size_t);
uint32_t ps2_sdk_fs_size(Ps2SdkFile *,uint64_t *);
uint32_t ps2_sdk_fs_resize(Ps2SdkFile *,uint64_t);
uint32_t ps2_sdk_fs_flush(Ps2SdkFile *);
uint32_t ps2_sdk_fs_close(Ps2SdkFile *);
#ifdef __cplusplus
}
#endif
#endif
