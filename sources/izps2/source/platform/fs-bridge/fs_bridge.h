#pragma once
#include "sdk_fs.h"
#define PS2_FS_PATH_MAX 769
struct Ps2FsMount {const char *logical;const char *physical;bool writable;};
/* Quiescent startup only; mounts are copied. No Linux FILE is passed to SDK.
 * The platform's fopencookie must produce its own FILE for core callers. */
bool ps2_install_file_bridge(Ps2SdkFs *,const Ps2FsMount *,size_t count);
