#ifndef IZ_PS2_EXECUTION_PROFILE_H
#define IZ_PS2_EXECUTION_PROFILE_H
/* Exact immutable R176 exports, matched to its pinned source manifest. */
enum {
 IZ_PS2_EXPORT_G_THREADS,
 IZ_PS2_EXPORT_G_NCHUNKS,
 IZ_PS2_EXPORT_G_USED_PAGES,
 IZ_PS2_EXPORT_G_MEMORY_POISONED,
 IZ_PS2_EXPORT_PS2_SW_TEMPORAL_STATISTICS,
 IZ_PS2_EXPORT_G_PHDRS,
 IZ_PS2_EXPORT_HBXL_EXITING,
 IZ_PS2_EXPORT_PS2_SESSION_REQUEST,
 IZ_PS2_EXPORT_PS2_SESSION_STATUS,
 IZ_PS2_EXPORT_PS2_SESSION_PRESENTED_FRAMES,
 IZ_PS2_EXPORT_PS2_EXIT_REQUESTED,
 IZ_PS2_EXPORT_PS2_HBXL_AV_TRACE,
 IZ_PS2_EXPORT_PS2_GS_PORTABLE_STATS,
 IZ_PS2_EXPORT_G_SDK_STACK_MAPS,
 IZ_PS2_EXPORT_G_SDK_STACK_SCOPES_ENTERED,
 IZ_PS2_EXPORT_G_STACK_CREATOR_COUNT,
 IZ_PS2_EXPORT_G_SDK_FAILED_REQUESTS,
 IZ_PS2_EXPORT_G_SDK_LAST_REQUEST,
 IZ_PS2_EXPORT_G_SDK_LAST_RESULT,
 IZ_PS2_EXPORT_PS2_HBXL_PUB_TIMING,
 IZ_PS2_EXPORT_PS2_HBXL_EE_WAIT_TALLY,
 IZ_PS2_EXPORT_PS2_LOCAL_STORAGE_STATUS,
 IZ_PS2_EXPORT_PS2_LOCAL_CONSOLE,
 IZ_PS2_EXPORT_PS2_LOCAL_RECEIPTS,
 IZ_PS2_EXPORT_PS2_NATIVE_PAD_SNAPSHOT,
 IZ_PS2_EXPORTS
};
typedef struct {uint32_t offset,bytes,code;} IzPs2Export;
static const IzPs2Export iz_ps2_exports[IZ_PS2_EXPORTS]={
 {2088032,1024,0}, /* g_threads */
 {1296972,4,0}, /* g_nchunks */
 {1296960,8,0}, /* g_used_pages */
 {1296928,4,0}, /* g_memory_poisoned */
 {69230472,40,0}, /* ps2_sw_temporal_statistics */
 {1295328,280,0}, /* g_phdrs */
 {1296192,4,0}, /* hbxl_exiting */
 {395148,708,1}, /* ps2_session_request */
 {395856,396,1}, /* ps2_session_status */
 {395128,20,1}, /* ps2_session_presented_frames */
 {2553064,4,0}, /* ps2_exit_requested */
 {90560,1472,0}, /* ps2_hbxl_av_trace */
 {69230528,64,0}, /* ps2_gs_portable_stats */
 {1296400,8,0}, /* g_sdk_stack_maps */
 {1296392,8,0}, /* g_sdk_stack_scopes_entered */
 {1296408,4,0}, /* g_stack_creator_count */
 {1296384,8,0}, /* g_sdk_failed_requests */
 {1296376,8,0}, /* g_sdk_last_request */
 {1296368,4,0}, /* g_sdk_last_result */
 {22912,512,0}, /* ps2_hbxl_pub_timing */
 {90112,256,0}, /* ps2_hbxl_ee_wait_tally */
 {2457856,128,0}, /* ps2_local_storage_status */
 {24512,65584,0}, /* ps2_local_console */
 {2404288,53528,0}, /* ps2_local_receipts */
 {92032,128,0}, /* ps2_native_pad_snapshot */
};
#endif
