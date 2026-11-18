#ifndef IZPSX_SD_STORAGE_SB4_H
#define IZPSX_SD_STORAGE_SB4_H
#include <sb4/native-app.h>
#include "fs-dir.h"
#include "sd-layout.h"

/* Translate the existing absolute module paths to the SDK's relative names.
 * Validate both a caller's narrower subtree and the owning module root. */
static inline const char *ps1_sb4_relative(const char *root,const char *path) {
    if (!ps1_sd_path_valid(root,path) || !ps1_sd_path_valid(IZPSX_SD_ROOT,path)) return NULL;
    const char *name=path+sizeof(IZPSX_SD_ROOT)-1;
    return *name=='/' ? name+1 : name;
}
static inline const Sb4StorageFiles *ps1_sb4_files(const Ps1SdDirectory *sd) {
    return sb4StorageFiles(sb4ModuleStorage(sd->app));
}
static inline uint32_t ps1_sb4_receipt(Ps1SdDirectory *sd,uint32_t result) {
    sd->transport=0;
    sd->calls++;
    return sd->error=result;
}
#endif
