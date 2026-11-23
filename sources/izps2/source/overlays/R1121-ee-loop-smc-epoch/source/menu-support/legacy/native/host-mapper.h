#ifndef IZ_HOST_MAPPER_H
#define IZ_HOST_MAPPER_H
#include "host-rpc.h"
#include "../relocatable/address.h"
typedef struct {IzHostRpcPlan plan;IzAddressCursor address;u32 offer,phase;} IzHostRpcMapper;
/* These functions run only in the supervisor's acknowledged display loan.
 * They use retained private state and never call application code. */
void iz_host_rpc_mapper_step(IzContext*,IzHostRpcOwner*,IzHostRpcMapper*,u32);
void iz_host_rpc_mapper_cancel(IzContext*,IzHostRpcOwner*,IzHostRpcMapper*,u32);
#ifndef __aarch64__
/* Host models translate only the target-ISA symbol-address boundary. */
u64 iz_host_rpc_code_symbol(u32);
#endif
#endif
