#ifndef IZPSX_SD_BRIDGE_H
#define IZPSX_SD_BRIDGE_H
#include "native-app.h"
typedef struct {
 uint64_t stack_top,state;
 IzNativeImage engine;
 uint64_t ticket;
 uint32_t phase,error,initialized,reload;
 IzNativeImageProgress progress;
} IzpsxBridge;
/* Bridge occupies the first page; engine state is independent of image BSS. */
#define IZPSX_STATE_OFFSET 4096u
#define IZPSX_APP_DATA_BYTES (256u*1024u)
#define IZPSX_PRIVATE_BYTES (8u*1024u*1024u)
#endif
