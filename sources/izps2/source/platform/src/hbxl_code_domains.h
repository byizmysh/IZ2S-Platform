#ifndef PS2_HBXL_CODE_DOMAINS_H
#define PS2_HBXL_CODE_DOMAINS_H
#include "hbxl_code_memory.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Separate v2 profile. Main EE/IOP/VU and a single Software GS worker own
 * disjoint page-aligned caches. MTVU, threaded IOP and SW rasterizer workers
 * must remain disabled. Writers serialize; other-domain RX stays executable.
 * A real MTGS ShutdownThread return precedes whole-arena release. */
#define PS2_HBXL_CODE_DOMAINS_FLAG 4u
typedef struct {
    Ps2HbxlCodeConfig base;
    uint64_t gs_offset,gs_bytes;
} Ps2HbxlCodeDomainsConfig;
typedef struct {
    Ps2HbxlCodeApi code;
    void (*sleep_ns)(uint64_t);
} Ps2HbxlCodeDomainsApi;
typedef struct {
    Ps2HbxlCodeStatistics code;
    uint64_t gs_owner,gs_offset,gs_bytes,ee_publications,gs_publications,writer_waits;
    uint32_t gs_joined,last_domain_error;
} Ps2HbxlCodeDomainsStatistics;
uint32_t ps2_hbxl_code_domains_bind(const Ps2HbxlCodeDomainsConfig *,const Ps2HbxlCodeDomainsApi *);
uint32_t ps2_hbxl_code_domains_initialize(const Ps2HbxlCodeDomainsConfig *);
const Ps2HbxlCodeDomainsConfig *ps2_hbxl_code_domains_default_config(void);
uint32_t ps2_hbxl_code_domains_register_gs(void);
/* Only the EE owner calls this AFTER actual MTGS ShutdownThread has returned.
 * It asserts a caller-established join; it does not perform or invent a join. */
uint32_t ps2_hbxl_code_domains_gs_joined(void);
/* The isolated pthread adapter reports the actual per-handle join result.
 * Observe only; never changes that result or invents thread completion. */
void ps2_hbxl_code_domains_observe_join(uint64_t,int);
uint32_t ps2_hbxl_code_domains_shutdown(void);
void ps2_hbxl_code_domains_statistics(Ps2HbxlCodeDomainsStatistics *);
/* Private serialized code-host hooks; not a general code_host replacement. */
int ps2_code_domains_scope_allowed(uintptr_t,size_t);
int ps2_code_domains_writer_wait(void);
#ifdef __cplusplus
}
#endif
#endif
