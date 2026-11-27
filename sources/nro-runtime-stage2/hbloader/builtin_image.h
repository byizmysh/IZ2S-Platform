#pragma once
#include <stddef.h>
#include <stdint.h>
/* Copies the immutable built-in runtime into loader-owned staging memory.
 * Does not access SD or execute an image; the existing ldr:ro cycle owns that. */
int hbloaderCopyBuiltin(void *staging,size_t capacity,const void *image,size_t bytes);
