#ifndef PS1_RESIDENT_H
#define PS1_RESIDENT_H
#include "platform.h"
#include "native-menu.h"
#include "resident-launcher.h"
#define PS1_RESIDENT_MAGIC 0x50535231u
/* All pointers target allocations outside the image's resettable data region. */
typedef struct {
 uint32_t magic,version,state,error;
 uint64_t data_base,data_region,pristine,data_bytes;
 uint64_t sd_directory,entries,entry_capacity;
 uint64_t games,returns,menu_frames,host_calls_after_start;
 ResidentLauncher launcher;
 uint64_t game_commands[8],menu_commands[8];
 uint64_t last_game_frames,last_game_seconds_ticks;
 char selected[PS1_SD_PATH_MAX];
} Ps1Resident;
_Static_assert(sizeof(Ps1Resident)<=4096,"resident supervisor config page");
uint32_t ps1_game_main(HeavyContext*);
uint32_t ps1_resident_main(Ps1Context*);
int ps1_resident_pause(Ps1Context*);
/* Trusted bootstrap contract: declared ranges must describe live owned
 * allocations, callbacks must be qualified executable entrypoints, strings
 * must terminate within their owned buffers, and opaque GPU/SDK objects keep
 * their external lifetime. Layout checks prevent reset overlap; they do not
 * validate arbitrary virtual pointers or provide a shared-process sandbox. */
#endif
