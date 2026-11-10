#ifndef IZ_NATIVE_RECOVERY_SERVER_H
#define IZ_NATIVE_RECOVERY_SERVER_H
#include "wire.h"
#include "journal.h"
#define IZ_RECOVERY_IO_AGAIN (-1)
#define IZ_RECOVERY_IO_FAILED (-2)
typedef struct {
 int (*listen)(void *);
 int (*accept)(void *,int listener);
 ptrdiff_t (*receive)(void *,int,void *,size_t);
 ptrdiff_t (*send)(void *,int,const void *,size_t);
 void (*close)(void *,int);
 /* Synchronous single-owner native dispatch. Must fill the complete reply.
  * Nonzero means UNKNOWN outcome: freeze dispatch, retain the executing
  * receipt and never invoke this operation again. */
 int (*execute)(void *,const uint8_t request[0x8080],uint8_t response[IZ_RECOVERY_RESPONSE_BYTES]);
} IzRecoveryTransport;
typedef struct {
 int fd;uint32_t phase;size_t have,want,sent;uint64_t deadline;
 uint8_t nonce[32],input[IZ_RECOVERY_MAX_FRAME],output[IZ_RECOVERY_MAX_FRAME];
} IzRecoveryPeer;
typedef struct {
 IzRecoveryJournal journal;IzRecoveryTransport transport;void*owner;
 uint8_t key[32];uint64_t generation,retry_at,connections;
 int listener,active,stopped,frozen;
 IzRecoveryPeer peers[2];uint8_t response[IZ_RECOVERY_RESPONSE_BYTES];
} IzRecoveryServer;
int iz_recovery_server_init(IzRecoveryServer *,const uint8_t session[16],const uint8_t key[32],const IzRecoveryTransport *,void *owner);
void iz_recovery_server_tick(IzRecoveryServer *,uint64_t monotonic_ms);
void iz_recovery_server_wake(IzRecoveryServer *);
void iz_recovery_server_stop(IzRecoveryServer *);
#endif
