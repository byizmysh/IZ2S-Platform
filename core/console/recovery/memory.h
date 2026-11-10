#ifndef IZ_NATIVE_RECOVERY_MEMORY_H
#define IZ_NATIVE_RECOVERY_MEMORY_H
#include <stddef.h>
/* The owner supplies these routines when linking a freestanding image. */
void *memcpy(void *,const void *,size_t);
void *memset(void *,int,size_t);
int memcmp(const void *,const void *,size_t);
#endif
