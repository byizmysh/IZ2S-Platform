#ifndef PS2_HBXL_DATA_MEMORY_PREALLOCATED_H
#define PS2_HBXL_DATA_MEMORY_PREALLOCATED_H
#include "hbxl_data_memory.h"
#ifdef __cplusplus
extern "C" {
#endif
#define PS2_PREALLOC_MAIN_MAGIC 0x32504d50u
#define PS2_PREALLOC_RECEIPT_MAGIC 0x32524d50u
#define PS2_PREALLOC_VERSION 1u
#define PS2_PREALLOC_FLAGS 1u
#define PS2_PREALLOC_DESCRIPTOR_BYTES 136u
#define PS2_PREALLOC_RECEIPT_BYTES 112u
#define PS2_PREALLOC_CONTEXT_BYTES 4096u
#define PS2_PREALLOC_SDK_RX_BYTES 6164480u
enum Ps2PreallocatedMainState {
    PS2_PREALLOC_PREPARED=1,PS2_PREALLOC_CLAIMED=2,
    PS2_PREALLOC_RELEASED=3,PS2_PREALLOC_RETAINED=4
};
typedef struct {
    uint32_t magic,version,size,flags;
    uint64_t base,bytes,code_base,code_bytes,data_base,data_bytes;
    uint64_t context,context_bytes,sdk_base,sdk_bytes;
    uint64_t receipt,receipt_bytes,nonce[2],descriptor_self;
} Ps2HbxlPreallocatedMain;
typedef struct {
    uint32_t magic,version,size,state;
    uint64_t descriptor,base,bytes,nonce[2],context,code_base,data_base,sdk_base;
    uint64_t claims,free_calls;
    uint32_t last_error,reserved;
} Ps2HbxlPreallocatedReceipt;
/* Host allocates Main FIRST with exact SDK posix_memalign(4096,298844160),
 * before RX/staticRW/runtime. It then stages the immutable descriptor and
 * PREPARED receipt in distinct caller-owned4096-aligned pages. Code/data
 * sizes exactly match HBX3 raw sizes; exclusions round them up to4096.
 * Bind this descriptor in args[4], never an existing v4/profile silently.
 * PREPARED is not permission for the host to free while native work may run.
 * CLAIMED transfers ownership to the provider. RELEASED means guest SDKfree
 * already returned, so the host removes its record WITHOUT freeing again.
 * RETAINED also covers an in-flight void SDK call: a normal protected call
 * may restore CLAIMED; a fault/unknown return leaves RETAINED indefinitely.
 * A host must wait for an exact native join/receipt before ownership cleanup.
 */
struct hbx_ctx;
/* Explicit external-model/composition entry. api.allocate is NEVER called;
 * release/protect/fatal must be verified native wrappers or offline models.
 * The ordinary legacy initialize_callbacks export refuses this profile. */
uint32_t ps2_hbxl_preallocated_initialize_callbacks(
    const Ps2HbxlPreallocatedMain*,const struct hbx_ctx*,
    const Ps2HeapPagesApi*,uintptr_t sdk_base);
#ifdef __cplusplus
}
#endif
#endif
