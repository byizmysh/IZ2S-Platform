#ifndef IZ_RECOVERY_JOURNAL_H
#define IZ_RECOVERY_JOURNAL_H
#include <stdint.h>
#include <stddef.h>
#define IZ_RECOVERY_RESPONSE_BYTES (16u+0x8000u)
enum { IZ_RECOVERY_IDLE, IZ_RECOVERY_EXECUTING, IZ_RECOVERY_COMPLETED };
enum { IZ_RECOVERY_EXECUTE=1, IZ_RECOVERY_CACHED, IZ_RECOVERY_PENDING,
 IZ_RECOVERY_WRONG_SESSION, IZ_RECOVERY_WRONG_HASH, IZ_RECOVERY_WRONG_ORDER,
 IZ_RECOVERY_INVALID };
typedef struct {
 uint8_t session[16], hash[32];
 uint64_t sequence, executions;
 uint32_t state, response_bytes;
 uint8_t response[IZ_RECOVERY_RESPONSE_BYTES];
} IzRecoveryJournal;
/* Single control-thread writer. The authenticated decoder must verify the
 * complete packet, its session, length and digest before calling begin. There
 * is deliberately no time-based expiration and no filesystem dependency. */
void iz_recovery_init(IzRecoveryJournal *,const uint8_t session[16]);
uint32_t iz_recovery_begin(IzRecoveryJournal *,const uint8_t session[16],uint64_t sequence,const uint8_t hash[32]);
uint32_t iz_recovery_complete(IzRecoveryJournal *,const void *response,uint32_t bytes);
#endif
