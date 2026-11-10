#ifndef IZ_PS2_STORAGE_H
#define IZ_PS2_STORAGE_H
#include "../native/directory.h"
#define IZ_PS2_STORAGE_ROOTS 3u
#define IZ_PS2_STORAGE_GAMES 256u
typedef struct {char name[256];u64 bytes;u32 root,reserved;} IzPs2Game;
enum {IZ_PS2_STORAGE_IDLE,IZ_PS2_STORAGE_SCANNING,IZ_PS2_STORAGE_CATALOG,IZ_PS2_STORAGE_OPENING,IZ_PS2_STORAGE_READY,IZ_PS2_STORAGE_CLOSING,IZ_PS2_STORAGE_CLOSED,IZ_PS2_STORAGE_FAILED,IZ_PS2_STORAGE_RETAINED,IZ_PS2_STORAGE_ENTERED};
typedef struct {
 u32 busy,state,error,phase,root_count,directory_index,row,row_count;
 u32 directory,game_handle,bios_handle,count,selected,bios_root,save_index,save_race,entered;
 u64 seen,rejected,units;
 IzFs*fs;char roots[IZ_PS2_STORAGE_ROOTS][128];
 IzPs2Game games[IZ_PS2_STORAGE_GAMES],bios[IZ_PS2_STORAGE_ROOTS];
 uint16_t order[IZ_PS2_STORAGE_GAMES];u8 has_bios[IZ_PS2_STORAGE_ROOTS];
 IzFsDirectoryEntry rows[32];
} IzPs2Storage;
typedef struct {
 u64 read_cache,files,segments,mount,save_cache,save,sdk;
 u8 read_token[16],save_token[16];
} IzPs2StorageBindings;
typedef struct {u8 mount[4096],files[4096],segments[4096],save[4096];} IzPs2StoragePages;
/* Root paths are explicit physical module data folders, for example current
 * package and its previous stable data owner. Enumeration opens one directory
 * at a time and reads32 entries with sizes, without a stat per game. */
u32 iz_ps2_storage_begin(IzPs2Storage*,IzFs*,const char roots[][128],u32 count);
u32 iz_ps2_storage_step(IzPs2Storage*,u32 budget,u32*done);
u32 iz_ps2_storage_select(IzPs2Storage*,u32 sorted_index);
u32 iz_ps2_storage_cancel(IzPs2Storage*,u32 reason);
u32 iz_ps2_storage_entered(IzPs2Storage*);
/* Pure preparation of the unchanged PS2 IFile/save descriptors. Game/BIOS
 * child capabilities remain owned here and are lent to engine readers.
 * Cancellation after entry intent needs actual engine cleanup receipts. */
u32 iz_ps2_storage_pack(const IzPs2Storage*,const IzPs2StorageBindings*,IzPs2StoragePages*);
#endif
