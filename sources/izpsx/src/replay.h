#ifndef IZPSX_REPLAY_H
#define IZPSX_REPLAY_H
#include "fs-dir.h"
#define IZPSX_REPLAY_MAGIC 0x495a5058u
#define IZPSX_REPLAY_PATH 4096u
#define IZPSX_REPLAY_INPUT 8192u
#define IZPSX_REPLAY_MAX 3600u
typedef struct {uint32_t request,frames,buttons[IZPSX_REPLAY_MAX];char path[PS1_SD_PATH_MAX];} Ps1Replay;
/* Optional explicit finite qualification request, accepted only by the picker.
 * Host writes payload/header first and aligned magic last, waits for ack, and
 * never rewrites the payload before ack. Native play needs no host commands.
 * Returns 0 absent, 1 accepted, -1 rejected. All fields stay in owned buffers. */
int ps1_replay_take(Ps1Replay*,void*controls,size_t bytes,const char*root);
#endif
