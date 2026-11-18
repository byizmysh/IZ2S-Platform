#ifndef SB4_PRESENTATION_H
#define SB4_PRESENTATION_H

#include <stdint.h>

struct IzNativeApp;

enum { SB4_PRESENTATION_UNAVAILABLE = 0xe8d2, SB4_PRESENTATION_IMAGES = 8 };

/* Coherent module storage and draw-only commands for its original shaders.
 * Objects belong to the launcher. Use them only while holding the ticket. */
typedef struct {
    uint32_t version, bytes, image_count, reserved;
    uint64_t acquire, complete[SB4_PRESENTATION_IMAGES], commands[SB4_PRESENTATION_IMAGES];
    void *mapped;
    uint64_t mapped_bytes;
} Sb4PresentationTarget;

/* Optional IZlauncher import "sb4GetPresentationTarget". Call from the
 * serialized module callback after acquiring presentation and before starting
 * any worker. The worker can consume the copied descriptor until it joins. */
typedef uint32_t (*Sb4GetPresentationTarget)(struct IzNativeApp *, uint64_t ticket,
                                            Sb4PresentationTarget *);

/* Optional "sb4GetPresentationBuffer". The presentation owner supplies its
 * mapped address. The returned VkBuffer supports transfer writes and is valid
 * until the presentation worker joins. The owner synchronizes shader reads. */
typedef uint32_t (*Sb4GetPresentationBuffer)(struct IzNativeApp *, const void *mapped,
                                            uint64_t *buffer);

#endif
