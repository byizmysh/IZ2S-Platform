#include "installed-artwork.h"
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBIDEF extern __attribute__((visibility("hidden")))
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_SIMD
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_MAX_DIMENSIONS 1024
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "vendor/stb_image.h"
#pragma GCC diagnostic pop

enum { PIXEL_BYTES = STORE_ICON_SIZE * STORE_ICON_SIZE * 4, MAX_ICON_JPEG = 256 * 1024 };

static uint32_t read_u32(const uint8_t *b) {
    return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
}

static uint64_t read_u64(const uint8_t *b) {
    return (uint64_t)read_u32(b) | (uint64_t)read_u32(b + 4) << 32;
}

static bool read_at(FILE *file, uint64_t length, uint64_t offset, void *out, size_t bytes) {
    return offset <= length && bytes <= length - offset && offset <= INT32_MAX &&
           !fseek(file, (long)offset, SEEK_SET) && fread(out, 1, bytes, file) == bytes;
}

static bool read_iza(FILE *file, uint64_t length, uint32_t *pixels) {
    enum { TILE_SECTION = 176 + 8 * 16, TILE_BYTES = 131072, TILE_IMAGE = 65536 };
    uint8_t head[512];
    if (!read_at(file, length, 0, head, sizeof head) || memcmp(head, "IZSDAPP1", 8) ||
        read_u32(head + 8) < 1 || read_u32(head + 8) > 3 ||
        read_u32(head + 12) != sizeof head || read_u32(head + 16) != length) return false;
    uint32_t offset = read_u32(head + TILE_SECTION);
    return offset >= sizeof head && offset <= length && TILE_BYTES <= length - offset &&
           read_u32(head + TILE_SECTION + 4) == TILE_BYTES &&
           read_at(file, length, (uint64_t)offset + TILE_IMAGE, pixels, PIXEL_BYTES);
}

bool installed_artwork(FILE *file, uint64_t length, bool iza, uint32_t *pixels) {
    if (iza) return read_iza(file, length, pixels);
    uint8_t header[32], asset[24];
    if (!read_at(file, length, 0, header, sizeof header) || memcmp(header + 16, "NRO0", 4))
        return false;
    uint64_t base = read_u32(header + 24);
    if (base < sizeof header || !read_at(file, length, base, asset, sizeof asset) ||
        memcmp(asset, "ASET", 4) || read_u32(asset + 4) != 0) return false;
    uint64_t offset = read_u64(asset + 8), bytes = read_u64(asset + 16);
    if (offset < 56 || offset > length - base || !bytes || bytes > MAX_ICON_JPEG ||
        bytes > length - base - offset) return false;
    uint8_t *jpeg = malloc((size_t)bytes);
    if (!jpeg) return false;
    int width, height;
    uint8_t *decoded = read_at(file, length, base + offset, jpeg, (size_t)bytes) ?
        stbi_load_from_memory(jpeg, (int)bytes, &width, &height, NULL, 4) : NULL;
    free(jpeg);
    if (!decoded) return false;
    int longest = width > height ? width : height;
    int fitted_width = (width * STORE_ICON_SIZE + longest / 2) / longest;
    int fitted_height = (height * STORE_ICON_SIZE + longest / 2) / longest;
    if (!fitted_width) fitted_width = 1;
    if (!fitted_height) fitted_height = 1;
    int left = (STORE_ICON_SIZE - fitted_width) / 2;
    int top = (STORE_ICON_SIZE - fitted_height) / 2;
    if (fitted_width != STORE_ICON_SIZE || fitted_height != STORE_ICON_SIZE) memset(pixels, 0, PIXEL_BYTES);
    for (int y = 0; y < fitted_height; ++y)
        for (int x = 0; x < fitted_width; ++x) {
            int source_y = (2 * y + 1) * height / (2 * fitted_height);
            int source_x = (2 * x + 1) * width / (2 * fitted_width);
            const uint8_t *pixel = decoded + 4 * (source_y * width + source_x);
            pixels[(y + top) * STORE_ICON_SIZE + x + left] = read_u32(pixel);
        }
    stbi_image_free(decoded);
    return true;
}
