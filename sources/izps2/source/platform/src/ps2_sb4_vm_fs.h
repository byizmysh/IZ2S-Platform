#ifndef PS2_SB4_VM_FS_H
#define PS2_SB4_VM_FS_H

#include <cstdint>

/* Install the core HostVFS routes after binding the module. An empty image
 * selects BIOS-only mode. A module data root adds cards and save-state reads.
 * A virtual resources root maps read-only to the module's resources directory.
 * Enable background disc reads only when hosted threads can read this storage. */
bool ps2_sb4_vm_fs_install(const char *scratch, const char *image, const char *data_root = nullptr,
                           bool background_disc_reads = false, const char *resources = nullptr);
/* Called after the VM closes all media, before releasing the SB4 session. */
bool ps2_sb4_vm_fs_finish();

/* Optional module-owned directory for derived gzip indexes. Only the VM
 * owner selects the cache and opens it for update. */
const char *ps2_sb4_vm_fs_gzip_cache(const char *filename);

inline constexpr char Ps2Sb4StateStageSuffix[] = ".iz2s-stage";
bool ps2_sb4_vm_fs_state_is_scoped(const char *filename);
uint32_t ps2_sb4_vm_fs_prepare_state(const char *filename);
uint32_t ps2_sb4_vm_fs_publish_state(const char *filename, bool keep_backup);
void ps2_sb4_vm_fs_discard_state_stage(const char *filename);

#endif
