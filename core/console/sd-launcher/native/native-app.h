#ifndef IZ_NATIVE_APP_H
#define IZ_NATIVE_APP_H
#include <stdint.h>
#include <stddef.h>
/* Native SD module ABI 1. iz_app(app,event) is the first function in the PIC
 * image. Mutable globals belong in app->data, not in executable pages.
 * INIT and FRAME may return nonzero to request an exit. STOP must return zero
 * only after every module-owned worker/device has stopped using its memory. */
enum { IZ_NATIVE_INIT=1,IZ_NATIVE_FRAME=2,IZ_NATIVE_STOP=3 };
// Optional source flag. It describes virtual RAM storage; no owner inspection.
#define IZ_NATIVE_SOURCE_RAM 0x80000000u
/* Serialized app handles controller assignment and its own exit shortcut. */
#define IZ_NATIVE_MANAGED_INPUT 0x40000000u
#define IZ_NATIVE_CONNECTION_REQUEST 0x33524349u
typedef struct IzNativeApp IzNativeApp;
enum { IZ_AUDIO_INITIALIZE,IZ_AUDIO_OPEN,IZ_AUDIO_RATE,IZ_AUDIO_CHANNELS,IZ_AUDIO_FORMAT,
 IZ_AUDIO_BUFFER,IZ_AUDIO_APPEND,IZ_AUDIO_START,IZ_AUDIO_STOP,IZ_AUDIO_FLUSH,
 IZ_AUDIO_RELEASED,IZ_AUDIO_COUNT,IZ_AUDIO_CLOSE,IZ_AUDIO_FUNCTIONS };
typedef uint64_t (*IzNativeAudioFunction)(uint64_t,uint64_t,uint64_t,uint64_t);
typedef struct {uint32_t version,bytes;IzNativeAudioFunction functions[IZ_AUDIO_FUNCTIONS];} IzNativeAudio;
_Static_assert(IZ_AUDIO_FUNCTIONS==13&&sizeof(IzNativeAudio)==112,"Verified AudioOut service table ABI");
struct IzNativeApp {
 uint32_t magic,version,bytes,flags;
 void *data;uint64_t data_bytes;
 void *controls;uint64_t controls_bytes;
 void *owner;
 uint32_t (*read_file)(IzNativeApp*,const char*,uint64_t,void*,uint32_t,uint32_t*);
 uint32_t (*write_file)(IzNativeApp*,const char*,const void*,uint32_t);
 void (*request_exit)(IzNativeApp*);
 uint64_t (*buttons)(IzNativeApp*);
 uint64_t tick_hz;
 uint64_t frames,result;
};
_Static_assert(sizeof(IzNativeApp)==112,"Native SD module ABI 1");
/* One-shot supervisor request in the shared header. No controller shortcut or
 * PC callback is involved; the resident network worker retains the request. */
static inline void iz_native_request_connection(IzNativeApp*app){
 if(app&&app->controls&&app->controls_bytes>=1024)*(uint32_t*)((uint8_t*)app->controls+1008)=IZ_NATIVE_CONNECTION_REQUEST;
}
typedef struct {
 IzNativeApp base;
 /* ABI 2/3. Borrowed coherent GPU memory and a bootstrap-verified service
  * table. STOP must return every DSP descriptor before releasing this memory. */
 void *private_memory;uint64_t private_bytes;
 const IzNativeAudio *audio;
 uint64_t (*ticks)(IzNativeApp*);
 void (*sleep)(IzNativeApp*,uint64_t nanoseconds);
} IzNativeApp2;
_Static_assert(offsetof(IzNativeApp2,private_memory)==112,"Native SD module ABI 1 prefix");
_Static_assert(sizeof(IzNativeApp2)==152,"Native SD module ABI 2");
typedef struct {
 uint32_t version,bytes;
 void *code;uint64_t code_bytes;
 void *data;uint64_t data_bytes;
 void *entry,*spawn;
 uint64_t token;
} IzNativeImage;
_Static_assert(sizeof(IzNativeImage)==64,"Owned native engine image receipt");
/* ABI 3 preserves both earlier prefixes. Callbacks run only from the owning
 * application's serialized INIT/FRAME/STOP, never a worker. Loading maps and
 * fixes the image but does not call its entry. Before unloading, the app must
 * join every engine user and validate actual device/worker cleanup receipts.
 * STOP must unload its image; an outstanding image retains the whole app. */
typedef struct {
 IzNativeApp2 base;
 uint32_t (*load_image)(IzNativeApp*,const char*,IzNativeImage*);
 uint32_t (*unload_image)(IzNativeApp*,uint64_t token);
} IzNativeApp3;
_Static_assert(offsetof(IzNativeApp3,load_image)==152&&sizeof(IzNativeApp3)==168,"Native SD module ABI 3");
#define IZ_NATIVE_STOP_PENDING 0x495a0001u
enum {IZ_NATIVE_IMAGE_LOADING=1,IZ_NATIVE_IMAGE_READY,IZ_NATIVE_IMAGE_CLOSING,IZ_NATIVE_IMAGE_CLOSED,IZ_NATIVE_IMAGE_FAILED,IZ_NATIVE_IMAGE_RETAINED};
typedef struct {
 uint32_t version,bytes,state,stage,error,done;
 uint64_t read_bytes,total_bytes,cleared_bytes,mapped_pages,total_pages,work_units;
} IzNativeImageProgress;
_Static_assert(sizeof(IzNativeImageProgress)==72,"Cooperative native image progress");
/* ABI 4 preserves ABI 3. Begin reserves a ticket but performs no SD/RO work.
 * Step uses 1..64 bounded work units. Release starts cancellation/unloading;
 * continue stepping to a terminal progress state. Join engine users first.
 * STOP may return STOP_PENDING while stepping cleanup; the supervisor keeps
 * input/presentation alive and calls STOP again instead of calling FRAME. */
typedef struct {
 IzNativeApp3 base;
 uint32_t (*image_begin)(IzNativeApp*,const char*,uint64_t*ticket);
 uint32_t (*image_step)(IzNativeApp*,uint64_t ticket,uint32_t budget,IzNativeImageProgress*,IzNativeImage*);
 uint32_t (*image_release)(IzNativeApp*,uint64_t ticket);
} IzNativeApp4;
_Static_assert(offsetof(IzNativeApp4,image_begin)==168&&sizeof(IzNativeApp4)==192,"Native SD module ABI 4");
typedef uint32_t (*IzNativeEntry)(IzNativeApp*,uint32_t);
/* ABI 5 adds an owned worker for the current loaded engine image. All older
 * prefixes are unchanged. The image and context remain reserved until JOIN.
 * The environment is borrowed; do not close or free its capabilities. */
typedef struct {
 uint32_t version,bytes;
 uint64_t sdk,sdk_bytes,application_pid,ro_pid;
 uint32_t ro_handle,filesystem;
 uint64_t tick_hz;
} IzNativeEnvironment;
_Static_assert(sizeof(IzNativeEnvironment)==56,"Borrowed native environment");
enum { IZ_WORKER_RUNNING=1,IZ_WORKER_FINISHED=2,IZ_WORKER_JOINED=3 };
typedef struct {uint32_t version,bytes,state,reserved;uint64_t result;} IzNativeWorkerProgress;
_Static_assert(sizeof(IzNativeWorkerProgress)==24,"Native worker progress");
typedef struct {
 IzNativeApp4 base;
 const IzNativeEnvironment *environment;
 uint32_t (*worker_begin)(IzNativeApp*,uint64_t image_token,void *context,uint64_t context_bytes,uint64_t *ticket);
 uint32_t (*worker_poll)(IzNativeApp*,uint64_t ticket,IzNativeWorkerProgress*);
 uint32_t (*worker_join)(IzNativeApp*,uint64_t ticket,uint64_t *result);
} IzNativeApp5;
_Static_assert(offsetof(IzNativeApp5,environment)==192&&sizeof(IzNativeApp5)==224,"Native SD module ABI 5");
/* ABI 6 keeps every earlier prefix. Graphics metadata is current-session,
 * borrowed and immutable. Never destroy its device, pool, images or views.
 * Functions correspond to the already verified supervisor Vulkan bindings. */
enum {
 IZ_GFX_CREATE_SHADER,IZ_GFX_DESTROY_SHADER,IZ_GFX_CREATE_GRAPHICS,IZ_GFX_CREATE_COMPUTE,IZ_GFX_DESTROY_PIPELINE,
 IZ_GFX_CREATE_BUFFER,IZ_GFX_DESTROY_BUFFER,IZ_GFX_BUFFER_REQUIREMENTS,IZ_GFX_ALLOCATE_MEMORY,IZ_GFX_FREE_MEMORY,IZ_GFX_BIND_BUFFER,IZ_GFX_MAP_MEMORY,IZ_GFX_UNMAP_MEMORY,
 IZ_GFX_CREATE_SET_LAYOUT,IZ_GFX_DESTROY_SET_LAYOUT,IZ_GFX_CREATE_LAYOUT,IZ_GFX_DESTROY_LAYOUT,IZ_GFX_CREATE_POOL,IZ_GFX_DESTROY_POOL,IZ_GFX_ALLOCATE_SET,IZ_GFX_UPDATE_SET,
 IZ_GFX_ALLOCATE_COMMANDS,IZ_GFX_FREE_COMMANDS,IZ_GFX_BEGIN_COMMAND,IZ_GFX_END_COMMAND,IZ_GFX_BIND_SET,IZ_GFX_BIND_PIPELINE,IZ_GFX_DISPATCH,IZ_GFX_BARRIER,IZ_GFX_BEGIN_RENDER,IZ_GFX_DRAW,IZ_GFX_END_RENDER,IZ_GFX_EXECUTE,
 IZ_GFX_ACQUIRE,IZ_GFX_SUBMIT,IZ_GFX_PRESENT,IZ_GFX_QUEUE_IDLE,IZ_GFX_COUNT
};
typedef struct {
 uint32_t version,bytes,function_count,image_count;
 uint64_t vulkan,instance,physical,device,queue,swapchain,command_pool;
 uint64_t images[8],views[8],functions[IZ_GFX_COUNT];
 uint8_t memory_properties[520];
} IzNativeGraphics;
_Static_assert(sizeof(IzNativeGraphics)==1016,"Borrowed graphics metadata ABI");
typedef struct {
 IzNativeApp5 base;
 const IzNativeGraphics *graphics;
 uint32_t (*presentation_acquire)(IzNativeApp*,uint64_t *ticket);
 uint32_t (*presentation_release)(IzNativeApp*,uint64_t ticket);
} IzNativeApp6;
_Static_assert(offsetof(IzNativeApp6,graphics)==224&&sizeof(IzNativeApp6)==248,"Native SD module ABI 6");
/* Acquire only at a serialized callback boundary, with a ready owned image
 * and no worker yet. It pauses supervisor presentation while HID/callbacks
 * keep running. Release requires no owned worker and a real QueueWaitIdle.
 * The module additionally proves engine-specific nested joins/cleanup before
 * releasing. STOP success with a live presentation lease retains all owners. */
#endif
