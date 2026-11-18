#ifndef IZ_STORE_INSTALLED_ARTWORK_H
#define IZ_STORE_INSTALLED_ARTWORK_H

#include "store.h"
#include <stdio.h>

bool installed_artwork(FILE *file, uint64_t length, bool iza, uint32_t *pixels);

#endif
