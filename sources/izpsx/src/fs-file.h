#ifndef PS1_FS_FILE_H
#define PS1_FS_FILE_H
#include "fs-dir.h"
/* Read-only native IFileSystem/OpenFile. Returned IFile is owned by the caller
 * and must be passed to ps1_sd_closefile after its readers finish. Both output
 * values remain zero on failure. Missing optional files return the real FS
 * result; no process abort, retry, host request or implicit fallback occurs. */
uint32_t ps1_sd_openfile(Ps1SdDirectory *,const char *root,const char *path,
                        uint32_t *handle,uint64_t *size);
uint32_t ps1_sd_openfile_mode(Ps1SdDirectory *,const char *root,const char *path,
                             uint32_t mode,uint32_t *handle,uint64_t *size);
uint32_t ps1_sd_createfile(Ps1SdDirectory *,const char *root,const char *path,uint64_t size);
uint32_t ps1_sd_mkdir(Ps1SdDirectory *,const char *root,const char *path);
uint32_t ps1_sd_writefile(Ps1SdDirectory *,uint32_t handle,uint64_t offset,const void *,size_t);
uint32_t ps1_sd_flushfile(Ps1SdDirectory *,uint32_t handle);
uint32_t ps1_sd_resizefile(Ps1SdDirectory *,uint32_t handle,uint64_t size);
uint32_t ps1_sd_commit(Ps1SdDirectory *);
/* Refuses the borrowed filesystem handle. A close failure is retained in
 * directory.leaked_handle; subsequent open/stat requests are refused until
 * that same capability has been explicitly closed successfully. */
uint32_t ps1_sd_closefile(Ps1SdDirectory *,uint32_t handle);
uint32_t ps1_sd_statfile(Ps1SdDirectory *,const char *root,const char *path,
                        uint64_t *size);
#endif
