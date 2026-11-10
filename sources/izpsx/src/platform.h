#ifndef PS1_NATIVE_PLATFORM_H
#define PS1_NATIVE_PLATFORM_H
#include "abi.h"
#define PS1_FLAG_BIOS_REQUIRED 1u
#define PS1_FLAG_BIOS_LOADED 2u
#define PS1_FLAG_PAGE_PUBLICATION 4u
#define PS1_FLAG_RETURN_PICKER 8u
#define PS1_FLAG_CONTINUOUS 16u
#define PS1_FLAG_MEMORY_CARDS 128u
/* Bit 32 is reserved for the separately qualified profiler. */
typedef struct {uint64_t path,data,bytes,capacity;uint32_t writable,reserved;} Ps1File;
/* reserved=1: SDK path at data; reserved=2: a host-owned, read-only IFile
 * capability in data. The host keeps it alive until the native thread joins.
 * reserved=3: native-owned SD IFile opened from a validated path at data. */
typedef struct {uint64_t open,read,size,close,reads,bytes,opened,errors,enter,restore,send,buffer,capacity,directory;} Ps1FileIO;
_Static_assert(sizeof(Ps1FileIO)==112,"PS1 read-only FS ABI");
void ps1_ifile_reset(void);
uint32_t ps1_ifile_size(Ps1FileIO*,uint32_t,int64_t*);
uint32_t ps1_ifile_read(Ps1FileIO*,uint32_t,uint64_t,void*,uint64_t,uint64_t);
/* One optional record per completed frame. All timestamps use base.tick/hz.
 * Deltas cover retro_run plus audio commit/display; core_end excludes them. */
typedef struct {
 uint64_t begin,core_end,frame_end,jit_patches,fs_reads,fs_bytes,audio_appends,audio_empty_ticks;
} Ps1Performance;
_Static_assert(sizeof(Ps1Performance)==64,"PS1 per-frame profile ABI");
typedef struct {
 HeavyContext base;
 uint64_t imports,files,file_count,game_path;
 uint64_t jit_rx,jit_rw,jit_bytes,publish;
 uint64_t audio,audio_capacity,audio_bytes,audio_hash,audio_frames;
 uint32_t width,height,pixel_format,video_calls;
 uint64_t jit_flushes,jit_patches,jit_bytes_published;
 uint64_t log_bytes,input,interpreter,reserved;
 uint64_t jit_source,pid,ro_pid,send;
 uint32_t service,pages_mapped,mapping_unknown,transport;
 uint64_t autonomy,performance,performance_capacity,performance_count;
} Ps1Context;
_Static_assert(sizeof(Ps1Context)==512,"PS1 context ABI");
_Static_assert(offsetof(Ps1Context,jit_source)==432,"PS1 JIT source ABI");
_Static_assert(offsetof(Ps1Context,service)==464,"PS1 RO handle ABI");
extern Ps1Context *ps1_context;
/* Per-game counter; reset with the frontend image. The 32-bit ABI mirror
 * saturates, while resident reporting and pacing use this full counter. */
uint64_t ps1_game_frames_done(void);
void ps1_fatal(unsigned code) __attribute__((noreturn));
void ps1_log(const char *s);
void ps1_platform_init(void);
void *ps1_jit_rx(size_t bytes);
long ps1_jit_offset(void);
void ps1_jit_publish(void *start,void *end);
void ps1_jit_close(void);
void ps1_ro_map(void);
void ps1_ro_unmap(void);
int ps1_ro_publish(Ps1Context*c,uint64_t begin,uint64_t end);
extern uintptr_t ps1_imports[];
#endif
