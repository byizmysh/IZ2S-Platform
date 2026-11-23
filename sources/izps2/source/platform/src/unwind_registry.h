#ifndef PS2_UNWIND_REGISTRY_H
#define PS2_UNWIND_REGISTRY_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {uintptr_t text,data,function;} Ps2DwarfBases;
typedef struct {const void *fde;uintptr_t pc;size_t bytes;} Ps2UnwindFrame;
typedef struct {
    void (*register_frame)(const void *);
    void (*deregister_frame)(const void *);
    const void *(*find_fde)(const void *,Ps2DwarfBases *);
} Ps2UnwindApi;
typedef struct {
    Ps2UnwindApi api;
    const Ps2UnwindFrame *frames;
    unsigned char *registered;
    size_t count,live;
    uint32_t ready,poisoned,last_error;
    uint64_t registrations,deregistrations,lookups;
} Ps2UnwindRegistry;
enum {PS2_UNWIND_INVALID=0xf701,PS2_UNWIND_BUSY=0xf702,PS2_UNWIND_LOOKUP=0xf703,PS2_UNWIND_RETAINED=0xf704};
/* Own immutable .eh_frame and text only. The loader sorts frames by PC, supplies
 * zeroed state bytes and retains all buffers/code until unregister succeeds.
 * Supports bounded DWARF32 CIE v1/v3 with z/R/P/L/S augmentation and pcrel-sdata4/8
 * FDE addresses. Other encodings are rejected, never guessed. No SDK calls occur
 * before every record validates. Register each FDE, never the whole CIE table:
 * this is the pinned LLVM SDK ABI, not GNU libgcc's table-registration API.
 * Calls are serialized; all module workers must be joined before unregister.
 * No exception may escape into code whose unwind metadata is unregistered. */
uint32_t ps2_unwind_register(Ps2UnwindRegistry *,const Ps2UnwindApi *,const void *,size_t,
                           uintptr_t,size_t,const Ps2UnwindFrame *,size_t,unsigned char *);
uint32_t ps2_unwind_unregister(Ps2UnwindRegistry *);
#ifdef __cplusplus
}
#endif
#endif
