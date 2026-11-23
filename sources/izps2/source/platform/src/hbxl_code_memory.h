#ifndef PS2_HBXL_CODE_MEMORY_H
#define PS2_HBXL_CODE_MEMORY_H
#include "code_host.h"
#include "ro_backend.h"
#ifdef __cplusplus
extern "C" {
#endif
#define PS2_HBXL_CODE_MAGIC 0x32434a48u
enum {PS2_HBXL_CODE_SINGLE_EXECUTOR=1u,PS2_HBXL_CODE_EXCLUSIVE_RO=2u};
/* Caller owns both SDK-heap blocks throughout the module lifetime. RX is a
 * host-checked free VA reservation by convention, never a fake mmap result.
 * Keep the RO endpoint/process identities unchanged and exclusive until the
 * core stops and all mappings are proved removed. This profile only permits
 * the initializing CPU thread to execute/publish JIT code; no MTVU/IOP worker
 * or software-GS JIT executor. Other threads may run non-JIT device work. */
typedef struct {
    uint32_t magic,version,size,flags;
    uint64_t source,shadow,rx,bytes,pid,ro_pid,send;
    uint32_t service,reserved;
} Ps2HbxlCodeConfig;
typedef struct {
    uint64_t base,bytes;
    uint32_t type,attributes,permission,device_refs,ipc_refs,reserved;
} Ps2HbxlMemoryInfo;
typedef struct {
    uint32_t (*query)(Ps2HbxlMemoryInfo *,uint32_t *,uintptr_t);
    uint32_t (*send)(void *,uint64_t,uint32_t);
    uint64_t (*current)(void);
    Ps2CacheOperation clean,invalidate;
} Ps2HbxlCodeApi;
typedef struct {
    uint64_t source,shadow,rx,bytes,owner,maps,unmaps,publications;
    uint32_t initialized,poisoned,live_pages,mapped_pages,peak_pages,last_error,transport,service_result;
} Ps2HbxlCodeStatistics;
enum {PS2_HBXL_CODE_INVALID=0xf701,PS2_HBXL_CODE_BUSY=0xf702,PS2_HBXL_CODE_RANGE=0xf703,PS2_HBXL_CODE_SDK=0xf704};
/* Generic checked composition. The native adapter below supplies verified
 * SDK QueryMemory/send callbacks and real DC/IC cache maintenance. */
uint32_t ps2_hbxl_code_bind(const Ps2HbxlCodeConfig *,const Ps2HbxlCodeApi *);
uint32_t ps2_hbxl_code_shutdown(void);
void ps2_hbxl_code_statistics(Ps2HbxlCodeStatistics *);
/* Native HBXL adapter: config is in HBX3 ctx->args[1]. args[0] remains free
 * for the frontend. Initializes no thread, allocator or process privilege. */
const Ps2HbxlCodeConfig *ps2_hbxl_code_default_config(void);
uint32_t ps2_hbxl_code_initialize(const Ps2HbxlCodeConfig *);
#ifdef __cplusplus
}
#endif
#endif
