// SPDX-License-Identifier: GPL-3.0+
/* The unsigned-byte comparison used by the IZPS2 R799 ARM64 build. */
#include <sb4/memory.h>
#include <stdint.h>
#if defined(__aarch64__)
#include <arm_neon.h>
#endif

int sb4CompareMemory(const void *left, const void *right, size_t bytes) {
    const uint8_t *a = left;
    const uint8_t *b = right;
    if (a == b) return 0;
#if defined(__aarch64__)
    while (bytes >= 16) {
        const uint8x16_t equal = vceqq_u8(vld1q_u8(a), vld1q_u8(b));
        if (vminvq_u8(equal) != UINT8_MAX) break;
        a += 16;
        b += 16;
        bytes -= 16;
    }
#endif
    while (bytes) {
        const int difference = (int)*a++ - (int)*b++;
        if (difference) return difference;
        --bytes;
    }
    return 0;
}

void sb4CopyOpaqueRgba(void *destination, const void *source, size_t pixels) {
    uint8_t *out = destination;
    const uint8_t *in = source;
#if defined(__aarch64__) && defined(__AARCH64EL__)
    const uint8x16_t opaque = vreinterpretq_u8_u32(vdupq_n_u32(UINT32_C(0xff000000)));
    while (pixels >= 4) {
        vst1q_u8(out, vorrq_u8(vld1q_u8(in), opaque));
        out += 16;
        in += 16;
        pixels -= 4;
    }
#endif
    while (pixels) {
        out[0] = in[0];
        out[1] = in[1];
        out[2] = in[2];
        out[3] = UINT8_MAX;
        out += 4;
        in += 4;
        --pixels;
    }
}
