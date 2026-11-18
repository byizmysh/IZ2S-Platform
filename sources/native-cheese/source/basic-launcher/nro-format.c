#include "nro-format.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
bool iz_nro_header(const uint8_t *h, size_t available, uint64_t bytes) {
    if (!h || available < 128 || bytes < 128 || memcmp(h + 16, "NRO0", 4)) return false;
    const uint32_t image = le32(h + 24), mod = le32(h + 4);
    if (image < 4096 || image > bytes || image % 4096 || mod < 128 || mod > image - 28) return false;
    uint64_t end = 0;
    for (size_t i = 0; i < 3; ++i) {
        const uint32_t offset = le32(h + 32 + i * 8), size = le32(h + 36 + i * 8);
        if (offset != end || offset % 4096 || size % 4096 || (!i && size < 4096)) return false;
        end = (uint64_t)offset + size;
        if (end > image) return false;
    }
    return end == image && (uint64_t)image + le32(h + 56) <= UINT32_MAX;
}
bool iz_nro_suffix(const char *name) {
    const char *extension = strrchr(name, '.');
    if (!extension || strlen(extension) > 6) return false;
    char lower[7] = {0};
    for (size_t i = 0; extension[i]; ++i) lower[i] = (char)tolower((unsigned char)extension[i]);
    return !strcmp(lower, ".nro");
}
bool iz_nro_arguments(const char *path, char *arguments, size_t capacity) {
    if (!path || strncmp(path, "sdmc:/", 6) || strlen(path) >= 512) return false;
    for (const unsigned char *p = (const unsigned char *)path; *p; ++p)
        if (*p < 32 || *p == '"' || *p == '\\') return false;
    const int size = snprintf(arguments, capacity, "\"%s\"", path);
    return size >= 0 && (size_t)size < capacity;
}
