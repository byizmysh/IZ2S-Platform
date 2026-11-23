#ifndef PS2_HBXL_VM_FRONTEND_H
#define PS2_HBXL_VM_FRONTEND_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define PS2_HBXL_VM_MAGIC 0x324d5650u
#define PS2_HBXL_VM_NO_AV 1u
#define PS2_HBXL_VM_FLAG_FULL_AV 2u
#define PS2_HBXL_VM_FLAG_BIOS_ONLY 4u
#define PS2_HBXL_VM_DATA_BYTES 298844160ull
#define PS2_HBXL_VM_CODE_BYTES 97517568ull
/* HBX3 args[0] points to this immutable caller-owned launch description;
 * args[1] remains the checked JIT source/shadow/RX/held-RO description. */
typedef struct {
    uint32_t magic,version,size,flags;
    uint64_t data_bytes,code_bytes;
    char scratch[256],resources[256],bios_filename[256],iso[256],anchor[256];
} Ps2HbxlVmConfig;
/* AV launch modes, without changing version 1 / 1312-byte layout:
 * FULL_AV (2): absolute ISO is required; anchor is an optional absolute path.
 * FULL_AV|BIOS_ONLY (6): boot the selected BIOS without a disc or entry anchor;
 * every byte of both iso[256] and anchor[256] must be zero. This canonical
 * empty form rejects stale paths in reused launch storage. BIOS_ONLY alone,
 * NO_AV mixed with an AV mode, and unknown flags are not valid AV launches.
 * BIOS filename, absolute resources/scratch paths and exact memory capacities
 * remain mandatory in both modes. */
const Ps2HbxlVmConfig *ps2_hbxl_vm_configuration(int argc,char **argv);
int ps2_hbxl_vm_validate_configuration(const Ps2HbxlVmConfig *);
void ps2_hbxl_vm_memory_initialize(void);
void ps2_hbxl_vm_memory_shutdown(void);
void ps2_hbxl_vm_fs_initialize(const char *,const char *,const char *);
void ps2_hbxl_vm_fs_shutdown(void);
void ps2_hbxl_vm_stage(const char *);
__attribute__((noreturn)) void ps2_hbxl_vm_fatal(const char *,unsigned);
#ifdef __cplusplus
}
#endif
#endif
