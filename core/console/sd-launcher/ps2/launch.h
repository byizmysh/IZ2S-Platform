#ifndef IZ_PS2_LAUNCH_H
#define IZ_PS2_LAUNCH_H
#include "execution.h"
typedef struct IzPs2Launch IzPs2Launch;
typedef struct {
 IzNativeApp6*app;IzPs2Memory*memory;IzPs2Shadow*shadow;
 IzPs2Video*video;IzPs2Storage*storage;IzNativeImage image;
 uint64_t jit_rx;
 uint8_t main_nonce[16],pad_nonce[16],av_token[16],read_token[16],save_token[16];
} IzPs2LaunchPlan;
enum {IZ_PS2_LAUNCH_PUBLISHING=1,IZ_PS2_LAUNCH_EXECUTING,
 IZ_PS2_LAUNCH_RETIRING,IZ_PS2_LAUNCH_CLOSED,IZ_PS2_LAUNCH_RETAINED};
typedef struct {
 uint32_t version,bytes,state,phase,error,stop_requested,published,reserved;
 uint64_t units,cleared_bytes,published_bytes;
 IzPs2ExecutionProgress execution;
} IzPs2LaunchProgress;
/* Storage is a single controller-owned span inside public app.data. Four
 * ready resource owners and their stable provider contexts live outside this
 * span, in the same retained application lifetime. Begin allocates nothing
 * and performs no provider calls. It derives all ABI pages from those owners;
 * no prepacked pages or PC callback are accepted. Begin rejection takes no
 * ownership. After success retain storage and owners until CLOSED; a retained
 * result grants no disposal/retry permission.
 *
 * Step publishes at most 64 KiB in a work unit, or makes one mapping query,
 * plus one current-process identity check. Execution then uses the existing
 * real worker/cleanup controller. STOP during publication cancels its ready
 * owners without entering the engine. Audio arena, scanout/log/cache buffers
 * stay inside app.data until all engine users and their receipts retire. */
size_t iz_ps2_launch_bytes(void);
uint32_t iz_ps2_launch_begin(void*,size_t,const IzPs2LaunchPlan*);
uint32_t iz_ps2_launch_step(IzPs2Launch*,uint32_t,IzPs2LaunchProgress*);
uint32_t iz_ps2_launch_stop(IzPs2Launch*);
/* A read-only description for the surrounding native application. This
 * grants neither worker entry nor resource-release authority. */
uint32_t iz_ps2_launch_describe(IzPs2Launch*,IzPs2ExecutionPlan*);
#endif
