#ifndef PS2_NATIVE_LOG_SPOOL_H
#define PS2_NATIVE_LOG_SPOOL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* This is a new isolated interface, not a Horizon ABI. The caller owns every
 * buffer and callback provider for the entire unreleased lifetime. The core
 * does not allocate, log, sleep, issue syscalls, or call the SDK directly. */
enum Ps2LogIoState { PS2_LOG_IO_OK, PS2_LOG_IO_ERROR, PS2_LOG_IO_UNCERTAIN };
typedef struct Ps2LogIoResult {
    uint32_t state, code;
    size_t bytes; /* Write: exact known progress, including on known error. */
} Ps2LogIoResult;
enum Ps2LogThreadResult {
    PS2_LOG_THREAD_OK, PS2_LOG_THREAD_BUSY,
    PS2_LOG_THREAD_REJECTED, PS2_LOG_THREAD_UNCERTAIN
};
enum Ps2LogResult {
    PS2_LOG_OK, PS2_LOG_SEALED, PS2_LOG_TOO_LARGE,
    PS2_LOG_FAILED, PS2_LOG_BUSY, PS2_LOG_INVALID
};
enum Ps2LogStage {
    PS2_LOG_STAGE_NONE, PS2_LOG_STAGE_START, PS2_LOG_STAGE_WAIT,
    PS2_LOG_STAGE_OPEN, PS2_LOG_STAGE_WRITE, PS2_LOG_STAGE_FLUSH,
    PS2_LOG_STAGE_CLOSE, PS2_LOG_STAGE_JOIN, PS2_LOG_STAGE_CONTRACT
};

typedef struct Ps2LogSpoolOps {
    void *synchronization;
    void (*lock)(void *);
    void (*unlock)(void *);
    /* Called with lock held. Atomically release it and wait, then reacquire it
     * before returning (also on error). Spurious wakes are allowed. */
    int (*wait)(void *);
    void (*notify_all)(void *); /* Called with lock held. Must not block. */
    void *thread;
    /* Start exactly one owned worker. REJECTED proves it never existed.
     * BUSY is invalid here. Unknown start must be UNCERTAIN, never REJECTED. */
    uint32_t (*start)(void *, void (*entry)(void *), void *argument);
    /* Nonblocking: OK proves actual join, not merely an exit flag/drain ACK.
     * BUSY permits a later explicit poll. Any other result forbids replay. */
    uint32_t (*try_join)(void *);
    void *sink;
    /* Only the worker calls these, serially and without the core lock.
     * open ERROR proves no sink opened; UNCERTAIN may have acquired it.
     * write ERROR keeps a known-open sink; bytes reports known progress.
     * close ERROR proves still open; UNCERTAIN has unknown ownership.
     * Sink operations must have bounded/interruptible provider behavior.
     * Never recursively journal through this spool from its own sink. */
    Ps2LogIoResult (*open)(void *);
    Ps2LogIoResult (*write)(void *, const void *, size_t);
    Ps2LogIoResult (*flush)(void *);
    Ps2LogIoResult (*close)(void *);
} Ps2LogSpoolOps;

typedef struct Ps2LogSpoolConfig {
    Ps2LogSpoolOps ops;
    void *queue;
    size_t queue_bytes;
    void *batch;
    size_t batch_bytes;
    size_t max_record_bytes;
} Ps2LogSpoolConfig;

typedef struct Ps2LogSpoolSnapshot {
    uint64_t accepted_records, accepted_bytes, written_records, written_bytes;
    uint64_t last_accepted_sequence, last_written_sequence;
    uint64_t write_calls, short_writes, full_waits, rejected_after_seal;
    uint64_t queue_high_water, queued_payload_bytes;
    size_t queue_used, inflight_bytes, inflight_offset;
    uint32_t active_producers, waiting_producers;
    uint32_t sealed, drained, flushed, closed, worker_done, joined;
    uint32_t start_pending, started, never_started, sink_opened;
    uint32_t uncertain, error_stage, error_code, sink_call_active;
    uint32_t clean_stop, safe_to_release, resources_retained;
} Ps2LogSpoolSnapshot;

/* Public only so the caller can reserve storage; fields are private to core.
 * No persistent wire layout or existing context/header ABI is changed. */
typedef struct Ps2LogSpool {
    Ps2LogSpoolConfig config;
    Ps2LogSpoolSnapshot state;
    size_t head, tail;
    uint64_t batch_records, batch_last_sequence;
    uint32_t magic, join_attempted, join_in_progress, thread_uncertain;
} Ps2LogSpool;

/* Storage must initially be zeroed. Reinitializing a live instance is rejected.
 * Caller also owns lifetime exclusion: no API entry after storage is released. */
int ps2_log_spool_start(Ps2LogSpool *, const Ps2LogSpoolConfig *);
/* One atomic record admission, FIFO by returned sequence. Blocks only for
 * space. A record > max_record_bytes is explicitly rejected, never truncated.
 * Source bytes must remain valid for this call and must not alias storage. */
int ps2_log_spool_append(Ps2LogSpool *, const void *, size_t, uint64_t *sequence);
/* Nonblocking producer seal; wakes full-queue waiters. Accepted records drain.
 * This does not cancel a physical sink call or prove that its worker joined. */
int ps2_log_spool_seal(Ps2LogSpool *);
int ps2_log_spool_try_join(Ps2LogSpool *);
int ps2_log_spool_snapshot(Ps2LogSpool *, Ps2LogSpoolSnapshot *);

#ifdef __cplusplus
}
#endif
#endif
