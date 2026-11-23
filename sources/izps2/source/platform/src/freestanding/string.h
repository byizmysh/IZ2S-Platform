#ifndef PS2_RESIDENT_FREESTANDING_STRING_H
#define PS2_RESIDENT_FREESTANDING_STRING_H
/* Declarations for prelibc_bytes.c, used only by the resident helper.
 * The full emulator continues to use its regular C runtime headers. */
#include <stddef.h>
void *memcpy(void *restrict, const void *restrict, size_t);
void *memset(void *, int, size_t);
int memcmp(const void *, const void *, size_t);
int strcmp(const char *, const char *);
int strncmp(const char *, const char *, size_t);
size_t strnlen(const char *, size_t);
#endif
