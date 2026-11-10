#ifndef IZ_SD_ABI_H
#define IZ_SD_ABI_H
#include <stdint.h>
#include <stddef.h>
typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;
#define IZ_MAX_APPS 15
#define IZ_MAX_IMAGES 8
#define IZ_MAX_DIRS 128
#define IZ_HEADER_BYTES 512
#define IZ_STATE_BYTES 1024
#define IZ_TILE_BYTES 131072
#define IZ_ATLAS_BYTES 2097152
#define IZ_SHARED_BYTES 8388608
#define IZ_SD_ROOT "/IZ2S/Homebrew/Modules"
#define IZ_LIBRARY_MAX 16
#define IZ_LIBRARY_CAPACITY (2u*1024*1024+4096)
typedef struct {char id[48];u8 sha256[32];u32 tile_crc,reserved[3];} IzLibraryEntry;
typedef struct {char magic[8];u32 version,bytes,count,table_crc;u8 padding[488];} IzLibraryHeader;
typedef struct {u32 sequence,reserved;char id[48];u8 sha256[32];} IzInstallRequest;
typedef struct {char path[192];u64 address,bytes;} IzRamFile;
_Static_assert(sizeof(IzRamFile)==208,"RAM file descriptor ABI");
_Static_assert(sizeof(IzLibraryHeader)==512&&sizeof(IzLibraryEntry)==96&&sizeof(IzInstallRequest)==88,"SD picker ABI");
enum { SEC_STATE,SEC_DATA,SEC_VERTEX,SEC_FRAGMENT,SEC_COMPUTE,SEC_BEGIN,SEC_AFTER,SEC_PRIVATE,SEC_TILE,SEC_PAD,SEC_NATIVE_CODE,SEC_NATIVE_DATA,SEC_COUNT };
enum { IZ_APP_NATIVE=1,IZ_APP_PRIVATE_128=2,IZ_APP_AUDIO_OUT=4,IZ_APP_ASSETS=8,IZ_APP_ENGINE_IMAGE=16,IZ_APP_COOPERATIVE=32,IZ_APP_MANAGED=64,IZ_APP_WORKER=128,IZ_APP_PRESENTATION=256 };
typedef struct { u32 offset,bytes,crc32,reserved; } IzSection;
typedef struct {
 char magic[8]; u32 version,header_bytes,total_bytes,flags;
 /* Native data size in reserved[0]. With ASSETS: count/total bytes in
  * reserved[1:3], canonical companion SHA256 in padding[0:32]. */
 char id[48],name[64]; u32 private_bytes,groups[3],repeat,reserved[5];
 IzSection section[SEC_COUNT]; u8 padding[144];
} IzAppHeader;
_Static_assert(sizeof(IzAppHeader)==512,"SD app header ABI");
enum {
 F_SEND,F_CLOSE,F_ALLOCATE,F_FREE,F_SLEEP,F_TICK,F_PAD_HANDHELD,F_PAD_FULLKEY,F_PAD_DUAL,F_PAD_LEFT,F_PAD_RIGHT,F_TOUCH,
 F_CREATE_SHADER,F_DESTROY_SHADER,F_CREATE_GRAPHICS,F_CREATE_COMPUTE,F_DESTROY_PIPELINE,
 F_CREATE_BUFFER,F_DESTROY_BUFFER,F_BUFFER_REQUIREMENTS,F_ALLOCATE_MEMORY,F_FREE_MEMORY,F_BIND_BUFFER,F_MAP_MEMORY,F_UNMAP_MEMORY,
 F_CREATE_SET_LAYOUT,F_DESTROY_SET_LAYOUT,F_CREATE_LAYOUT,F_DESTROY_LAYOUT,F_CREATE_POOL,F_DESTROY_POOL,F_ALLOCATE_SET,F_UPDATE_SET,
 F_ALLOCATE_COMMANDS,F_FREE_COMMANDS,F_BEGIN_COMMAND,F_END_COMMAND,F_BIND_SET,F_BIND_PIPELINE,F_DISPATCH,F_BARRIER,F_BEGIN_RENDER,F_DRAW,F_END_RENDER,F_EXECUTE,
 F_ACQUIRE,F_SUBMIT,F_PRESENT,F_QUEUE_IDLE,F_COUNT
};
typedef struct IzContext IzContext;
typedef struct {
 IzContext *c; u32 handles[32],leaked; u8 *buffer;u32 ram_open[32];
} IzFs;
#include "native-app.h"
typedef struct {
 u64 source,destination,pages,mapped,data,data_bytes;
 union {IzNativeApp app;IzNativeApp2 app2;IzNativeApp3 app3;IzNativeApp4 app4;IzNativeApp5 app5;IzNativeApp6 app6;};
 IzFs *fs;
 char root[128];
 u32 initialized,unknown;
 IzNativeAudio audio;
 u64 image_state,image_token,image_generation;
 u32 image_busy,event_active,image_async,stopping;
 IzNativeEnvironment environment;
 u64 worker_thread,worker_entry,worker_context,worker_context_bytes,worker_result,worker_return,worker_token,worker_generation;
 u32 worker_state,worker_done;
 IzNativeGraphics graphics;
 u64 presentation_token,presentation_generation;
 u32 presentation_owned;
} IzNative;
/* These owners outlive iz_run when a child/result is still uncertain. */
typedef struct {IzFs fs;IzNative native;} IzOwner;
/* Private frame-plan ABI is also packed by the shared host bridge. */
typedef struct {
 u32 version,bytes;u64 controls,controls_bytes,commands[IZ_MAX_IMAGES];
 char id[64];u64 reserved[2];
} IzHostFramesPlan;
struct IzContext {
 u32 magic,version,stop,state,error,poisoned,active,count;
 u64 frames,transitions,read_bytes,read_calls,opened,closed;
 u32 probe,probe_result,rescan,scan_generation;
 u64 functions[F_COUNT];
 u64 device,queue,swapchain,acquired,command_pool,controls_buffer,controls,controls_bytes,menu_layout,menu_descriptor;
 u64 image_count,images[IZ_MAX_IMAGES],views[IZ_MAX_IMAGES],semaphores[IZ_MAX_IMAGES],menu_commands[IZ_MAX_IMAGES];
 u64 snapshot,scratch,arena,arena_bytes; u32 filesystem,memory_type;
 u64 tick_hz; u32 rejected,last_app_error;
 char last_id[48]; u32 probe_nonce,probe_crc;
 u64 stack,stack_bytes;
 u32 ro_handle,native_status;
 u64 pid,ro_pid,native_code_reads,native_launches;
 u64 recovery_wake;
 u32 catalog_revision,catalog_count;
 struct {char id[48],name[64];} catalog[IZ_MAX_APPS];
 u64 audio_functions[13];
 u64 native_data,native_data_bytes;
 u32 load_stage,staging_bytes;
 u32 library_revision,request_revision;
 u64 library_staging,library_capacity;
 u32 library_offer,library_applied,library_error,library_count;
 IzLibraryEntry library[IZ_LIBRARY_MAX];
 u32 request_sequence,request_head,request_count,request_ack,request_ack_error,request_error,picker_observed;
 IzInstallRequest requests[IZ_LIBRARY_MAX];
 u32 host_hold_request,host_hold_ready,host_hold_release;
 u32 auto_scan_signature,auto_scan_error;
 u32 remove_sequence,remove_error;
 char removed_id[48];
 /* An optional immutable parent menu, distinct from the mutable SD menu. */
 u64 parent_snapshot;
 /* Immutable session files inside this supervisor's owned RAM allocation. */
 u64 ram_base,ram_bytes,ram_files,ram_read_bytes;
 u32 ram_file_count,ram_only;
 char ram_app_id[48];
 /* Mixed menu: the immutable parent contains the host's original menu.
  * Host work may touch shared graphics only after display_ready is observed.
  * Release publishes a complete new host menu by advancing host_revision. */
 u32 host_mode,host_count,host_revision,host_applied;
 struct {char id[48],name[64];} host_catalog[IZ_MAX_APPS];
 u32 catalog_source[IZ_MAX_APPS];
 u32 host_display_request,host_display_ready,host_display_release;
 u32 host_launch,host_remove;
 u32 host_install_index,host_install_target,host_install_sequence,host_install_ack;
 /* Odd requests originate in the console menu; even claims originate on
  * the PC. Only the supervisor may issue a ready grant or reject a claim. */
 u32 host_display_claim,host_display_rejected,host_display_observed;
 /* Already verified SDK addresses. All older packing offsets stay fixed. */
 u64 sdk,worker_functions[3];
 u64 graphics_vulkan,graphics_instance,graphics_physical;
 u8 graphics_memory_properties[520];
 /* Launcher-owned persistence status; no manifest opt-in. */
 u32 checkpoint_enabled,checkpoint_state,checkpoint_error,checkpoint_kind,checkpoint_bank,checkpoint_reserved;
 u64 checkpoint_generation,checkpoint_logical,checkpoint_stored,checkpoint_time_bias,checkpoint_progress;
 u64 supervisor_base,supervisor_bytes;
 u32 host_frames_offer,host_frames_ack,host_frames_active,host_frames_paused;
 u32 host_frames_returned,host_frames_reason,host_frames_error,host_frames_cancel;
 IzHostFramesPlan host_frames_plan;
 IzOwner owner;
};
_Static_assert(sizeof(IzContext)<=12288,"Context overlaps bootstrap temporary space");
typedef u64 (*IzFunction)(u64,u64,u64,u64,u64,u64,u64,u64);
#define FN(c,n) ((IzFunction)(uintptr_t)(c)->functions[n])
#define CALL(c,n,a,b,d,e,f,g,h,i) FN(c,n)((u64)(uintptr_t)(a),(u64)(uintptr_t)(b),(u64)(uintptr_t)(d),(u64)(uintptr_t)(e),(u64)(uintptr_t)(f),(u64)(uintptr_t)(g),(u64)(uintptr_t)(h),(u64)(uintptr_t)(i))
void *memset(void*,int,size_t);
void *memcpy(void*,const void*,size_t);
int memcmp(const void*,const void*,size_t);
size_t strlen(const char*);
u32 iz_crc(const void*,size_t);
u32 iz_crc_update(u32,const void*,size_t);

u32 iz_fs_open(IzFs*,const char*,u32*,u64*);
u32 iz_fs_read(IzFs*,u32,u64,void*,u32);
u32 iz_fs_read_crc(IzFs*,u32,u64,void*,u32,u32);
u32 iz_fs_close(IzFs*,u32);
u32 iz_fs_dirs(IzFs*,char names[][48],u32*);
u32 iz_fs_probe(IzFs*);
u32 iz_fs_write_file(IzFs*,const char*,const void*,u32);
u32 iz_fs_checkpoint_begin(IzFs*,const char*,u32*);
u32 iz_fs_checkpoint_resize(IzFs*,u32,u64);
u32 iz_fs_checkpoint_write(IzFs*,u32,u64,const void*,u32);
u32 iz_fs_checkpoint_finish(IzFs*,u32);
u32 iz_fs_remove_app(IzFs*,const char*);
u32 iz_validate_header(const IzAppHeader*,u64);
void iz_library_tick(IzContext*);
void iz_library_menu(IzContext*);
void iz_menu_changed(IzContext*);

u32 iz_native_load(IzFs*,const IzAppHeader*,const u8*,IzNative*,void*,u64);
u32 iz_native_load_saved(IzFs*,const IzAppHeader*,const u8*,IzNative*,void*,u64,const void*);
u32 iz_native_event(IzNative*,u32);
u32 iz_native_unload(IzNative*);
#endif
