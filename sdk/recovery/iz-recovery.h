#ifndef IZ_RECOVERY_H
#define IZ_RECOVERY_H
/* Owned application telemetry, no code loading, memory addresses or commands.
 * Single owner calls tick/stop. A game can run this owner on a separate thread.
 * Never restore this object after a process restart: process_id must be fresh.
 */
#include <stdint.h>
#include <stddef.h>
#define IZR_PASSPORT_BYTES 4096
#define IZR_WIRE_BYTES 4136
struct izr_status {
 uint64_t heartbeat,completed,pending,used_bytes;
 uint32_t operation,catalog_revision,app_count;
 char active[64];
 unsigned char catalog_hash[32];
};
/* Application-owned, non-blocking stream. All callbacks run on the tick owner.
 * open: descriptor >= 0 or failure. connected: 1 ready, 0 pending, < 0 failed.
 * send/receive: byte count, IZR_IO_AGAIN, or IZR_IO_FAILED. A zero count is EOF.
 * The adapter owns platform errors and any socket-compatible bounce buffer.
 * It must not retain a data pointer after returning. No callback may block.
 */
#define IZR_IO_AGAIN ((ptrdiff_t)-1)
#define IZR_IO_FAILED ((ptrdiff_t)-2)
struct izr_transport {
 int (*open)(void *,uint32_t ipv4,uint16_t port);
 int (*connected)(void *,int descriptor);
 ptrdiff_t (*send)(void *,int descriptor,const unsigned char *,size_t);
 ptrdiff_t (*receive)(void *,int descriptor,unsigned char *,size_t);
 void (*close)(void *,int descriptor);
};
struct izr_client {
 unsigned char passport[IZR_PASSPORT_BYTES];
 unsigned char key[32],session[16],process_id[16],nonce[32];
 unsigned char wire[IZR_WIRE_BYTES];
 uint32_t ipv4;
 uint16_t port;
 int fd,phase,authenticated;
 size_t have,want,sent;
 uint64_t sequence,deadline,retry_at;
 uint32_t retry_ms;
 struct izr_transport transport;
 void *transport_context;
};
/* Host-order port, network-order IPv4. Credentials/proc_id supplied by the
 * application through trusted configuration and fresh process randomness.
 */
#ifndef IZR_NO_POSIX
int izr_init(struct izr_client *,uint32_t ipv4,uint16_t port,const unsigned char key[32],const unsigned char session[16],const unsigned char process_id[16]);
#endif
/* Native/embedded owners use their existing typed network API. Building with
 * IZR_NO_POSIX omits izr_init and every POSIX dependency, not the wire protocol.
 * A live client may not be reinitialized: stop it before replacing the owner.
 */
int izr_init_transport(struct izr_client *,uint32_t ipv4,uint16_t port,const unsigned char key[32],const unsigned char session[16],const unsigned char process_id[16],const struct izr_transport *,void *context);
/* Bounded non-blocking socket work; now_ms must use a monotonic clock. */
int izr_tick(struct izr_client *,uint64_t now_ms,const struct izr_status *);
void izr_stop(struct izr_client *);
#endif
