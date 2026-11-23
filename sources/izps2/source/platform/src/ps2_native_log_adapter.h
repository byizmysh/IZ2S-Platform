#ifndef PS2_NATIVE_LOG_ADAPTER_H
#define PS2_NATIVE_LOG_ADAPTER_H
#include "ps2_native_log_spool.h"
#include "native_sd_write.h"
#include <pthread.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct Ps2NativeLogAdapterConfig {
    Ps2SdWriteConfig sd; /* Borrowed FS, independent caller-owned 64KiB buffer. */
    Ps2SdSend send;
    Ps2SdClose close_handle;
    void *queue, *batch, *backend;
    size_t queue_bytes, batch_bytes, backend_bytes, max_record_bytes;
    size_t worker_stack_bytes;
    const char *unique_path; /* Exclusive create; never truncate an old file. */
    uint64_t (*tick)(void *);
    void *tick_user;
    uint64_t tick_hz; /* 0 means explicitly unqualified. */
} Ps2NativeLogAdapterConfig;

/* Separate append-only backend ledger. Payload is unmodified OP_LOG bytes
 * (kind 1), or a fixed 64-bit sink/control receipt (kind 2). Records are only
 * observationally readable after committed==1 with acquire semantics. */
typedef struct Ps2NativeLogBackendHeader {
    uint32_t magic, kind, bytes, committed;
    uint64_t sequence, tick;
    int32_t fd;
    uint32_t reserved;
} Ps2NativeLogBackendHeader;
typedef struct Ps2NativeLogBackendReceipt {
    uint64_t operation, phase, argument0, argument1;
    uint64_t state, code, bytes, tick_hz;
} Ps2NativeLogBackendReceipt;
typedef struct Ps2NativeLogAdapter {
    Ps2LogSpool spool;
    Ps2SdWrite sd;
    Ps2NativeLogAdapterConfig config;
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    pthread_t worker;
    void (*worker_entry)(void *);
    void *worker_argument;
    uint64_t file, offset;
    char path[0x301];
    size_t backend_used;
    uint64_t backend_sequence, backend_rejected, capture_failures;
    uint32_t initialized, active, sticky_error, controller_join_attempted;
    uint32_t controller_join_available, controller_join_result;
} Ps2NativeLogAdapter;
typedef struct Ps2NativeLogAdapterSnapshot {
    Ps2LogSpoolSnapshot spool;
    uint64_t file, offset, backend_sequence, backend_rejected, capture_failures;
    size_t backend_used;
    uint32_t active, sticky_error, controller_join_attempted;
    uint32_t backendRequiresPreservation; /* Not a durable SD receipt. */
} Ps2NativeLogAdapterSnapshot;

/* New candidate API, not inserted in the existing VM lifecycle. Requires its
 * ordinary private pthread provider and SD capability to be initialized.
 * One global activation only; inactive bootstrap logs still use legacy HBFS. */
int ps2_native_log_adapter_start(Ps2NativeLogAdapter *, const Ps2NativeLogAdapterConfig *);
int ps2_native_log_adapter_seal(Ps2NativeLogAdapter *);
/* Blocking existing pthread_join, admitted only after core worker_done.
 * Drain/exit flags are never substituted for the actual join return. */
int ps2_native_log_adapter_join(Ps2NativeLogAdapter *);
int ps2_native_log_adapter_snapshot(Ps2NativeLogAdapter *, Ps2NativeLogAdapterSnapshot *);
/* VFS hook: 0 only when never activated; 1 accepted; -1 sticky failure.
 * After activation, even failure/closed state must NEVER fall back to TCP. */
int ps2_native_log_capture(int fd, const void *, size_t);
#ifdef __cplusplus
}
#endif
#endif
