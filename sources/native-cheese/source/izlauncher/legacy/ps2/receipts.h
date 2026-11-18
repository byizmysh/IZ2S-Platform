#ifndef IZ_PS2_RECEIPTS_H
#define IZ_PS2_RECEIPTS_H
#include "startup.h"
#include "receipt-json.h"
typedef struct {uint32_t id,state,bytes,error;uint8_t json[4096];} IzPs2ReceiptRow;
typedef struct {
 uint32_t magic,version,bytes,phase,error,count,present,reserved;
 uint64_t context,code_base,code_bytes,data_base,data_bytes;
 IzPs2ReceiptRow rows[13];
} IzPs2ReceiptCapsule;
_Static_assert(sizeof(IzPs2ReceiptRow)==4112&&offsetof(IzPs2ReceiptCapsule,rows)==72&&sizeof(IzPs2ReceiptCapsule)==53528,"Original image-owned capsule ABI");
typedef struct {uint64_t context,code_base,code_bytes,data_base,data_bytes;} IzPs2ReceiptImage;
typedef struct {
 uint32_t mode,configure_attempted,closed,create_returned,pending,retained,uncertain,pending_mask,diagnostic_write_failed;
 int32_t create_result,last_wait,last_reset,final_idle;uint32_t stats_available;
 uint64_t accepted,fence_completed,idle_completed,enqueued,returns,wait_calls,wait_timeouts,reset_calls;
 uint64_t final_idle_calls,destroy_calls,destroy_returns,current_fence,created_fence,legacy_returns;
} IzPs2ReceiptFence;
typedef struct {uint64_t epoch,queued,completed,read,presenter_accepted,dropped,discarded,bootstrap_syncs;} IzPs2ReceiptReadback;
typedef struct {
 uint32_t version,bytes,error,transport_validated;
 IzPs2ReceiptImage image;
 uint64_t created,joined,code_maps,code_unmaps,data_allocations,data_releases;
 uint64_t tls_threads,tls_allocations,tls_releases,sd_opened,sd_closed;
 uint64_t audio_opens,audio_closes,audio_workers,audio_joins,gs_handle;
 uint64_t instance,device,submit_epoch,idle_epoch,sentinels_created,sentinels_destroyed;
 uint64_t presented,audio_appends,iop_starts,iop_owner,iop_mask;
 uint32_t vm_passed,domains_production,gs_joined,last_domain_error,av_passed,queue_idle,av_retained;
 uint32_t after_static_dtors,hwgs_closed,private_device_closed,exit_receipt_present;
 IzPs2ReceiptFence fence;IzPs2ReceiptReadback readback;
} IzPs2Receipts;
typedef struct {Rj json;IzPs2Receipts candidate;} IzPs2ReceiptScratch;
/* Pure, normal-current-profile record validation. Call only on an immutable
 * copy after the real owning SDK join and before unmapping current image/data.
 * This proves no join, process identity, live globals, Main, guards, original
 * descriptors or resource release. Those remain mandatory controller checks.
 * Unknown/failure/pre-GS receipt paths are retained, not declared clean.
 * Source-produced canonical integer/ASCII keys only; escaped keys and JSON
 * deeper than16 or larger than512 nodes are rejected before interpreting it.
 * Rejection clears output except error; input/output/workspace aliases do not
 * mutate any bytes. No libc, heap, SDK, GPU, filesystem or engine calls. */
uint32_t iz_ps2_decode_receipts(IzPs2Receipts*,IzPs2ReceiptScratch*,const IzPs2ReceiptCapsule*,const IzPs2ReceiptImage*);
uint32_t iz_ps2_validate_storage_status(const uint8_t status[128]);
#endif
