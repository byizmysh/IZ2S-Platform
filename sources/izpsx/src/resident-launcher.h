#ifndef PS1_RESIDENT_LAUNCHER_H
#define PS1_RESIDENT_LAUNCHER_H
#include <stdint.h>
#define RESIDENT_LAUNCHER_MAGIC 0x495a5053u
#define RESIDENT_MENU_BYTES 0x7f0000u
enum { RESIDENT_CONTINUE=0,RESIDENT_SELECT_PS1=1,RESIDENT_ERROR=2,RESIDENT_STOP=3 };
enum { RESIDENT_IDLE=0,RESIDENT_RESTORING=1,RESIDENT_MENU=2,RESIDENT_SELECTED=3,RESIDENT_FAILED=4,RESIDENT_STOPPED=5 };
/* Exactly the native Npad state prefix and TouchScreenState<16> ABI. */
typedef struct {
 uint64_t sample;
 uint32_t buttons,upper;
 int32_t lx,ly,rx,ry;
 uint32_t attributes,reserved[7];
} ResidentPad;
typedef struct {uint64_t sample;uint32_t count,reserved;uint8_t points[640];} ResidentTouch;
typedef struct {
 uint32_t magic,version,status,error;
 uint64_t snapshot,storage,storage_bytes,storage_capacity;
 uint32_t ps1_slot,stop_request;
 uint64_t pad[5],touch,render,render_argument,tick,sleep,tick_hz;
 /* All mutable state is caller-owned, outside the per-game .data/.bss reset. */
 uint64_t entries,restored_bytes,frames,pad_samples,touch_samples,rejected,errors,late_frames;
 uint64_t deadline,last_tick,extension_flags_address;
 uint32_t observed_buttons,last_request,input_armed,period_remainder,extension_flags_saved,reserved2;
} ResidentLauncher;
_Static_assert(sizeof(ResidentPad)==64,"native Npad ABI");
_Static_assert(sizeof(ResidentTouch)==656,"native touch ABI");
_Static_assert(sizeof(ResidentLauncher)==256,"resident launcher config ABI");
/* SDK pad callbacks: void (ResidentPad*, const uint32_t* id); handheld id32,
 * four legacy controller styles id0. Optional touch: void (ResidentTouch*).
 * Render callback: uint32_t (void *render_argument), returns 0 after GPU idle.
 * Tick: uint64_t (void); sleep: void (uint64_t nanoseconds).
 * The caller must own the only graphics worker and idle its queue before entry;
 * snapshot/storage/config must remain alive outside all per-game resets.
 * frame() is exposed for model testing and external native event loops. run()
 * has no frame/time limit: it waits until PS1 is selected or rendering fails.
 */
uint32_t resident_launcher_enter(ResidentLauncher*);
uint32_t resident_launcher_frame(ResidentLauncher*);
uint32_t resident_launcher_run(ResidentLauncher*);
#endif
