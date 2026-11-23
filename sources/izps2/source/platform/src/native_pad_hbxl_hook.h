#ifndef PS2_NATIVE_PAD_HBXL_HOOK_H
#define PS2_NATIVE_PAD_HBXL_HOOK_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define PS2_HBXL_PAD_MAGIC 0x32445048u
/* Optional immutable HBX3 args[5], caller owns the exclusive Pad0 input lease.
 * Absent, or flags0 with zero remaining fields, is explicitly disabled.
 * flags1 enables the exact current SDK RX; token is an ownership receipt,
 * never an authorization or an independently proven OS-wide input lock. */
typedef struct {
    uint32_t magic,version,size,flags;
    uint64_t sdk_rx,sdk_bytes,lease_token[2];
} Ps2HbxlPadConfig;
enum {PS2_HBXL_PAD_NEW=0,PS2_HBXL_PAD_DISABLED=1,PS2_HBXL_PAD_ACTIVE=2,PS2_HBXL_PAD_STOPPED=3};
typedef struct {
    uint32_t phase,error,enabled,sdk_verified;
    uint64_t original_polls,applied_polls,pre_init_skips,post_shutdown_skips;
    uint64_t pad_initializations,pad_shutdowns,owner_thread;
} Ps2HbxlPadStatistics;
int ps2_hbxl_pad_validate_configuration(const Ps2HbxlPadConfig *);
/* One lifecycle per image. Native initialization occurs after original
 * Pad::Initialize, before the first original CPU input-poll boundary. */
uint32_t ps2_hbxl_pad_initialize(void);
uint32_t ps2_hbxl_pad_statistics(Ps2HbxlPadStatistics *);
/* Required wrappers: InputManager::PollSources, Pad::Initialize, Pad::Shutdown.
 * Original polling executes first on its caller's CPU pthread. Pad0 lifecycle
 * and all SIO readers must be quiescent; IOP mode Off is enforced downstream. */
#ifdef __cplusplus
}
#endif
#endif
