#ifndef PS2_HBXL_VM_SD_FS_H
#define PS2_HBXL_VM_SD_FS_H
#include "native_ifile.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    uint64_t opened,closed,game_bytes,bios_bytes,game_max_end,physical_reads;
    uint32_t active_streams,poisoned,error,transport,unexpected_handles;
} Ps2HbxlVmSdFsStatistics;
/* Alternative composition for the existing VM FS exports. HBX3 args[3] is a
 * Ps2IFileMountDescription; omit hbxl_vm_fs.cpp from this candidate. */
void ps2_hbxl_vm_sd_fs_statistics(Ps2HbxlVmSdFsStatistics*);
/* Additional wraps: stat, open, fopen. Protected SD files never acquire a
 * POSIX descriptor, so mmap cannot accidentally consume an HBFS/fake fd. */
#ifdef __cplusplus
}
#endif
#endif
