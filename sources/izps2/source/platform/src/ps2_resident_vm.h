#ifndef PS2_RESIDENT_VM_H
#define PS2_RESIDENT_VM_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* New process-owner interface. It changes no existing VM or AV wire layout.
 * All functions below run on the persistent CPU owner, outside JIT scopes.
 * The caller must use real VM Shutdown + MTGS drain before ending an epoch.
 * Successful end is an idle-between-games receipt, never final free authority.
 */
typedef struct Ps2ResidentAvEpoch {
    uint64_t epoch;
    uint64_t frames_begin, frames_current, appends_begin, appends_current;
    uint64_t opens_begin, opens_current, closes_begin, closes_current;
    uint64_t workers_begin, workers_current, joins_begin, joins_current;
    uint32_t active, ended, ready, queue_idle, error, retained;
} Ps2ResidentAvEpoch;
int ps2_hbxl_vm_av_begin_game(uint64_t epoch);
int ps2_hbxl_vm_av_end_game(uint64_t epoch);
void ps2_hbxl_vm_av_game_statistics(Ps2ResidentAvEpoch *out);

enum { PS2_RESIDENT_VM_STOP = 0, PS2_RESIDENT_VM_START = 1 };
enum {
    PS2_RESIDENT_VM_CLEAN = 0, PS2_RESIDENT_VM_STARTUP_FAILED = 1,
    PS2_RESIDENT_VM_RUNTIME_FAILED = 2, PS2_RESIDENT_VM_VERIFY_FAILED = 3
};
/* The default weak selector stops after one game, preserving ordinary app
 * behavior. An autonomous native selector overrides it: show resident menu,
 * enumerate native SD and return the next choice, without polling a server.
 * flags use the existing FULL_AV/BIOS_ONLY VM flags. ISO/BIOS capabilities
 * must already belong to the persistent native filesystem. No file or
 * resource capability is granted by a path or by this callback's result.
 */
typedef struct Ps2ResidentVmSelection {
    uint32_t version, size, flags, reserved;
    char iso[256], resume[512];
} Ps2ResidentVmSelection;
int ps2_resident_vm_select_next(uint64_t completed_epoch, int game_result,
                               Ps2ResidentVmSelection *choice);
#ifdef __cplusplus
}
static_assert(sizeof(Ps2ResidentAvEpoch) == 128, "Resident AV epoch ABI");
static_assert(sizeof(Ps2ResidentVmSelection) == 784, "Resident selection ABI");
#else
_Static_assert(sizeof(Ps2ResidentAvEpoch) == 128, "Resident AV epoch ABI");
_Static_assert(sizeof(Ps2ResidentVmSelection) == 784, "Resident selection ABI");
#endif
#endif
