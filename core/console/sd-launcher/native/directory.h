#ifndef IZ_SD_DIRECTORY_H
#define IZ_SD_DIRECTORY_H
#include "abi.h"
/* Exact Horizon DirectoryEntry wire prefix. One read returns at most32 rows
 * with sizes, so native pickers do not issue a stat for every candidate. */
typedef struct {char name[0x301];u8 reserved[3],type,padding[3];u64 bytes;} IzFsDirectoryEntry;
_Static_assert(sizeof(IzFsDirectoryEntry)==0x310&&offsetof(IzFsDirectoryEntry,type)==0x304&&offsetof(IzFsDirectoryEntry,bytes)==0x308,"Directory entry ABI");
/* All child handles belong to IzFs until closed. Opening and size querying
 * are separate bounded operations for cooperative native controllers.
 * Existing file methods retain the module-root path policy. Directory and
 * mkdir/type operations accept canonical absolute paths on the borrowed SD
 * filesystem; their controller must choose its explicit data/save roots. */
u32 iz_fs_open_handle(IzFs*,const char*,u32*);
u32 iz_fs_size(IzFs*,u32,u64*);
u32 iz_fs_directory_open(IzFs*,const char*,u32 mode,u32*);
u32 iz_fs_directory_read(IzFs*,u32,IzFsDirectoryEntry*,u32 capacity,u32*);
u32 iz_fs_entry_type(IzFs*,const char*,u32*);
u32 iz_fs_mkdir(IzFs*,const char*);
#endif
