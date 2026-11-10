#ifndef IZ_RECOVERY_NATIVE_ABI_H
#define IZ_RECOVERY_NATIVE_ABI_H
#include <stdint.h>
#include <stddef.h>
#include "../server.h"
enum { NET_SOCKET,NET_BIND,NET_LISTEN,NET_ACCEPT,NET_POLL,NET_RECV,NET_SEND,NET_CLOSE,NET_FCNTL,NET_ERRNO,NET_SLEEP,NET_TICK,NET_ADDRESS,NET_SENDTO,NET_STATUS,NET_CURRENT,NET_COUNT };
typedef struct {
 uint32_t magic,version,stop,state,error,port,wake,ip;
 uint64_t sdk,scratch,scratch_bytes,stack,stack_bytes;
 uint8_t session[16],key[32];
 uint64_t functions[NET_COUNT],dispatches,network_changes;
 uint64_t beacon_sequence,beacon_packets;uint32_t host_ips[8],host_count,discovery_port;int32_t udp_fd;uint32_t network_type,network_status,reserved;
 uint64_t foreground_thread,suspend_thread,resume_thread,thread_type,worker_ticks;
 uint32_t foreground_policy,foreground_state;
 uint32_t qualification_stage,qualification_done;
 IzRecoveryServer server;
} IzRecoveryNative;
typedef uint64_t (*IzNetFunction)(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
#define NET(c,n,a,b,d,e,f,g) ((IzNetFunction)(uintptr_t)(c)->functions[n])((uint64_t)(uintptr_t)(a),(uint64_t)(uintptr_t)(b),(uint64_t)(uintptr_t)(d),(uint64_t)(uintptr_t)(e),(uint64_t)(uintptr_t)(f),(uint64_t)(uintptr_t)(g))
uint64_t iz_net_run(IzRecoveryNative *);
#endif
