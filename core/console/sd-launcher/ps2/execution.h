#ifndef IZ_PS2_EXECUTION_H
#define IZ_PS2_EXECUTION_H
#include "memory.h"
#include "jit-shadow.h"
#include "video.h"
#include "storage.h"
typedef struct IzPs2Execution IzPs2Execution;
typedef struct {
 IzNativeApp6*app;IzPs2Memory*memory;IzPs2Shadow*shadow;
 IzPs2Video*video;IzPs2Storage*storage;
 IzPs2StartupInput startup;IzPs2AvInput av;uint64_t main_receipt;
} IzPs2ExecutionPlan;
enum {IZ_PS2_EXECUTION_ADMIT=1,IZ_PS2_EXECUTION_RUNNING,IZ_PS2_EXECUTION_JOINING,
 IZ_PS2_EXECUTION_VERIFYING,IZ_PS2_EXECUTION_RETIRING,IZ_PS2_EXECUTION_CLOSED,
 IZ_PS2_EXECUTION_RETAINED};
typedef struct {
 uint32_t version,bytes,state,phase,error,joined,proof_valid,stop_requested;
 uint64_t units,worker_ticket,presentation_ticket;
} IzPs2ExecutionProgress;
/* A serialized native controller over already prepared, current owners and
 * published original ABI pages. Storage must remain allocated until CLOSED;
 * RETAINED never authorizes disposal or a new launch. Zero storage once only.
 * Its opaque state records actual begin/poll/join outcomes, not caller flags.
 * Begin does no provider work. One step unit performs at most one provider
 * operation/query or bounded local proof, with a fresh self identity check.
 * No SDK or engine address is supplied by a PC. */
size_t iz_ps2_execution_bytes(void);
uint32_t iz_ps2_execution_begin(void*,size_t,const IzPs2ExecutionPlan*);
uint32_t iz_ps2_execution_step(IzPs2Execution*,uint32_t budget,IzPs2ExecutionProgress*);
uint32_t iz_ps2_execution_stop(IzPs2Execution*);
#endif
