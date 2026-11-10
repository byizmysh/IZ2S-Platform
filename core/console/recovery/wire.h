#ifndef IZ_NATIVE_RECOVERY_WIRE_H
#define IZ_NATIVE_RECOVERY_WIRE_H
#include <stdint.h>
#include <stddef.h>
#define IZ_RECOVERY_HEADER_BYTES 128u
#define IZ_RECOVERY_MAX_PAYLOAD 0x8080u
#define IZ_RECOVERY_MAX_FRAME (IZ_RECOVERY_HEADER_BYTES+IZ_RECOVERY_MAX_PAYLOAD)
enum { IZ_RECOVERY_CHALLENGE=1,IZ_RECOVERY_PROOF,IZ_RECOVERY_RECEIPT,IZ_RECOVERY_REQUEST,IZ_RECOVERY_RESPONSE,IZ_RECOVERY_PING,IZ_RECOVERY_DISCOVERY };
typedef struct {uint32_t type,bytes;uint64_t sequence;const uint8_t*payload;uint8_t hash[32];} IzRecoveryFrame;
/* frame_size: 0 needs the rest of the header; -1 rejects the header. A
 * positive length never means authenticated or eligible for execution. */
int iz_recovery_frame_size(const void *,size_t have);
int iz_recovery_decode(IzRecoveryFrame *,const void *,size_t bytes,const uint8_t session[16],const uint8_t nonce[32],const uint8_t key[32]);
size_t iz_recovery_encode(void *,size_t capacity,uint32_t type,uint64_t sequence,const uint8_t session[16],const uint8_t nonce[32],const uint8_t key[32],const void *payload,uint32_t bytes);
#endif
