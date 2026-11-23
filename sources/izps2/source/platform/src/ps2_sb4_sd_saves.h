#ifndef PS2_SB4_SD_SAVES_H
#define PS2_SB4_SD_SAVES_H

#include "sdk_fs.h"

struct IzNativeApp;

#ifdef __cplusplus
extern "C" {
#endif

/* Bind before engine file users start. Both disc I/O and saves use this
 * binding until ps2_sd_saves_native_shutdown() after all workers join. */
int ps2_sb4_sd_saves_attach(struct IzNativeApp *app);
const Ps2SdkFsApi *ps2_sb4_sd_saves_files(void);
/* Release the binding after the core has closed all media and saves. */
uint32_t ps2_sb4_sd_saves_detach(struct IzNativeApp *app);

#ifdef __cplusplus
}
#endif
#endif
