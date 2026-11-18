#pragma once
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Like memcmp, compares bytes as unsigned and stops at the first difference. */
int sb4CompareMemory(const void *left, const void *right, size_t bytes);

/* Copy a row of RGBA8 pixels and set every destination alpha byte to 255.
 * Both buffers must contain pixels * 4 bytes and must not overlap. */
void sb4CopyOpaqueRgba(void *destination, const void *source, size_t pixels);

#ifdef __cplusplus
}
#endif
