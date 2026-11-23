#ifndef IZ_PS2_STARTUP_H
#define IZ_PS2_STARTUP_H
#include "../native/native-app.h"

/* Original R176 engine geometry, independently matched to its emitted ELF.
 * These are logical ABI lengths; the image receipt describes rounded owners. */
#define IZ_PS2_CODE_BYTES UINT64_C(39693208)
#define IZ_PS2_DATA_BYTES UINT64_C(69238416)
#define IZ_PS2_MAIN_BYTES UINT64_C(66060288)
#define IZ_PS2_JIT_BYTES UINT64_C(110100480)
#define IZ_PS2_GS_OFFSET UINT64_C(108003328)
#define IZ_PS2_STACK_BYTES UINT64_C(1048576)
#define IZ_PS2_SDK_BYTES UINT64_C(6164480)

enum {IZ_PS2_STARTUP_HWGS=1,IZ_PS2_STARTUP_HOST_FILES=2};
/* Addresses denote caller-owned/borrowed resources, never allocation or
 * execution permission. The eventual owner must qualify actual mappings,
 * device identities and all immutable AV/FS/HWGS descriptors before entry. */
typedef struct {
 uint32_t version,bytes,flags,reserved;
 IzNativeEnvironment environment;
 IzNativeImage image;
 uint64_t context,stack,control,log,ring,dispatch;
 uint64_t vm,domains,av,filesystem,main_descriptor,pad,save,hwgs;
 uint64_t main,jit_source,jit_shadow,jit_rx;
 uint64_t gpu_instance,gpu_physical,gpu_device;
 uint8_t main_nonce[16],pad_nonce[16];
 char paths[5][256]; /* scratch, resources, BIOS filename, ISO, optional anchor */
} IzPs2StartupInput;
typedef struct {
 uint8_t context[4096],vm[4096],domains[4096],main[4096];
 uint8_t main_receipt[4096],pad[4096],control[4096],log[4096];
} IzPs2StartupPages;

/* Pure packing; no SDK, allocation, engine entry or release authority.
 * All inputs are validated before any output byte is changed. Output pages
 * must subsequently be copied to their exact live, admitted destinations.
 * The receipt address is supplied separately because it is not an HBX arg. */
uint32_t iz_ps2_pack_startup(const IzPs2StartupInput*,uint64_t main_receipt,
                           IzPs2StartupPages*);
#endif
