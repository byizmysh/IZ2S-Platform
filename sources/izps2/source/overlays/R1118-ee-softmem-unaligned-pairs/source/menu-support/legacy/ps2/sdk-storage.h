#ifndef IZ_PS2_SDK_STORAGE_H
#define IZ_PS2_SDK_STORAGE_H
#include "sdk-memory.h"
#include "storage.h"
/* A controller-owned FS ledger over the supervisor's borrowed SD filesystem.
 * Child handles are tracked here; the borrowed filesystem is never closed. */
typedef struct {IzContext context;IzFs filesystem;} IzPs2StorageIo;
u32 iz_ps2_sdk_storage_bind(IzPs2StorageIo*,const IzNativeEnvironment*,void*scratch,u64 bytes);
#endif
