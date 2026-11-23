#ifndef PS2_HBXL_VM_AV_H
#define PS2_HBXL_VM_AV_H
#include "native_av_bindings.h"
#ifdef __cplusplus
extern "C" {
#endif
#define PS2_HBXL_VM_AV_MAGIC 0x32415650u
#define PS2_HBXL_VM_AV_SCRATCH_BYTES (1024ull*1056*4)
/* Immutable caller-owned HBX3 args[2]. Actual mapping/ownership admission is
 * the launcher's responsibility; range checks cannot prove a mapping exists. */
typedef struct {
    uint32_t magic,version,size,flags;
    uint64_t vulkan_rx,vulkan_rx_bytes,audio_arena,audio_arena_bytes;
    uint64_t scratch,scratch_bytes;
    Ps2LauncherVideoLease video;
} Ps2HbxlVmAvConfig;
typedef struct {
    uint64_t presented,audio_appends,audio_opens,audio_closes,workers,joins;
    uint32_t error,initialized,private_worker_tls_checked,audio_closed_joined;
    uint32_t gs_joined,queue_idle,retained,shutdown;
} Ps2HbxlVmAvStatistics;
int ps2_hbxl_vm_av_validate_configuration(const Ps2HbxlVmAvConfig *);
/* After private HBXL libc/thread/emulated-TLS startup, before SPU2 Initialize.
 * Uses actual SDK AudioOut callbacks, but ONLY private HBXL pthread workers. */
int ps2_hbxl_vm_av_initialize(void);
int ps2_hbxl_vm_av_ready(void);
int ps2_hbxl_vm_av_error(void);
/* Serial MTGS refresh of the cached game image/menu while the VM is paused. */
int ps2_hbxl_vm_av_refresh(void);
/* After real VM Shutdown, CPUThreadShutdown and vu1Thread.Close, before the
 * domains/memory shutdown. Requires exact domains v4 join and code receipts.
 * No host-owned GPU/context/arena resource is destroyed by this function. */
int ps2_hbxl_vm_av_shutdown(void);
void ps2_hbxl_vm_av_statistics(Ps2HbxlVmAvStatistics *);
/* Required additional --wrap symbols:
 * _Z7GSvsyncjb, _ZN4SPU25CloseEv, _ZN11AudioStream16CreateNullStreamEjj.
 * Also link the four existing live AudioStream factory/control wrappers. */
#ifdef __cplusplus
}
#endif
#endif
