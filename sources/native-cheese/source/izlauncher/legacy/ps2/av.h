#ifndef IZ_PS2_AV_H
#define IZ_PS2_AV_H
#include <stdint.h>
#include <stddef.h>
#define IZ_PS2_VIDEO_BYTES UINT64_C(4198400)
#define IZ_PS2_AUDIO_BYTES UINT64_C(36864)
#define IZ_PS2_SCANOUT_BYTES UINT64_C(4325376)
typedef struct {uint64_t base,bytes;} IzPs2AvSpan;
typedef struct {
 uint64_t buffer,memory,set_layout,layout,pool,descriptor;
 uint64_t mapped,mapped_bytes,commands[8],semaphores[9];
 uint32_t memory_flags,reserved;
} IzPs2VideoSource;
/* Pure description of already owned resources. This is not a resource or
 * cleanup receipt. The eventual controller must create/admit both complete
 * Vulkan resource graphs and borrow current-session graphics identities.
 * No flags or caller-supplied booleans can grant execution through this API. */
typedef struct {
 uint32_t version,bytes,image_count,reserved;
 uint64_t av_page,vulkan,device,queue,swapchain,audio,scratch,scratch_bytes;
 uint8_t token[16];
 IzPs2VideoSource sources[2];
 uint32_t protected_count,reserved2;
 IzPs2AvSpan protected[24];
} IzPs2AvInput;
/* Produces the unchanged R176 288-byte AV prefix and 528-byte pair tail in
 * one zero-padded page. All validation precedes every output write. No SDK,
 * FS, GPU calls, allocation, engine entry or cleanup authority. */
uint32_t iz_ps2_pack_av(const IzPs2AvInput*,uint8_t out[4096]);
#endif
