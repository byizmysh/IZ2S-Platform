#ifndef IZ_INSTRUCTION_CHECKPOINT_H
#define IZ_INSTRUCTION_CHECKPOINT_H
#include <stdint.h>
/* Private protocol between the launcher and its own instruction executor.
 * This is not an app manifest capability or a public native ABI event. */
#define IZ_INSTRUCTION_CAPTURE 0x495a4301u
#define IZ_INSTRUCTION_SAVED 0x495a4353u
#define IZ_INSTRUCTION_BUSY 0xe731u
#define IZ_INSTRUCTION_MAILBOX 3584u
typedef struct {
 uint32_t magic,version,done_rules,audio_open,audio_started,reserved[3];
} IzInstructionSaved;
_Static_assert(sizeof(IzInstructionSaved)==32,"Portable instruction progress");
#endif
