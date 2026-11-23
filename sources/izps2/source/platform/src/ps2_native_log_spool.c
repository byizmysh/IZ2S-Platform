#include "ps2_native_log_spool.h"
#include <string.h>

#define MAGIC 0x31474f4cu
#define HEADER 16u
#define CONTRACT_CODE 0xffff0001u
#define WAIT_CODE 0xffff0002u

static int range(const void *p, size_t n) {
    return p && n && (uintptr_t)p <= UINTPTR_MAX - n;
}
static int overlaps(const void *a, size_t an, const void *b, size_t bn) {
    return (uintptr_t)a < (uintptr_t)b + bn && (uintptr_t)b < (uintptr_t)a + an;
}
static void lock(Ps2LogSpool *s) { s->config.ops.lock(s->config.ops.synchronization); }
static void unlock(Ps2LogSpool *s) { s->config.ops.unlock(s->config.ops.synchronization); }
static void notify(Ps2LogSpool *s) { s->config.ops.notify_all(s->config.ops.synchronization); }
static void fail(Ps2LogSpool *s, uint32_t stage, uint32_t code, int uncertain) {
    if (!s->state.error_stage) {
        s->state.error_stage = stage;
        s->state.error_code = code ? code : CONTRACT_CODE;
    }
    if (uncertain) s->state.uncertain = 1;
    s->state.sealed = 1;
    notify(s);
}
static int wait_locked(Ps2LogSpool *s) {
    if (!s->config.ops.wait(s->config.ops.synchronization)) return 0;
    fail(s, PS2_LOG_STAGE_WAIT, WAIT_CODE, 1);
    return -1;
}
static void refresh(Ps2LogSpool *s) {
    Ps2LogSpoolSnapshot *v = &s->state;
    const int no_data = !v->queue_used && !v->inflight_bytes &&
        v->accepted_bytes == v->written_bytes && v->accepted_records == v->written_records;
    v->drained = v->sealed && no_data;
    const int no_sink = !v->sink_opened || v->closed;
    const int no_thread = v->never_started || v->joined;
    v->safe_to_release = v->sealed && !v->start_pending && no_thread &&
        !v->active_producers && no_data && no_sink && !v->uncertain;
    v->clean_stop = v->safe_to_release && !v->error_stage && v->flushed && v->closed;
    v->resources_retained = !v->safe_to_release;
}
static void queue_copy(Ps2LogSpool *s, void *out, const void *input, size_t n, int put) {
    size_t *at = put ? &s->tail : &s->head;
    unsigned char *q = s->config.queue;
    unsigned char *d = out;
    const unsigned char *p = input;
    while (n) {
        size_t k = s->config.queue_bytes - *at;
        if (k > n) k = n;
        if (put) { memcpy(q + *at, p, k); p += k; }
        else { memcpy(d, q + *at, k); d += k; }
        *at += k;
        if (*at == s->config.queue_bytes) *at = 0;
        n -= k;
    }
}
static void peek_header(Ps2LogSpool *s, uint64_t h[2]) {
    const size_t old = s->head;
    queue_copy(s, h, 0, HEADER, 0);
    s->head = old;
}
static int io_result(Ps2LogSpool *s, Ps2LogIoResult r, uint32_t stage) {
    if (r.state == PS2_LOG_IO_OK && !r.code && !r.bytes) return 0;
    fail(s, stage, r.code, r.state != PS2_LOG_IO_ERROR || r.bytes != 0);
    return -1;
}
static void worker_done(Ps2LogSpool *s) {
    s->state.sink_call_active = 0;
    s->state.worker_done = 1;
    refresh(s);
    notify(s);
}
static void worker(void *argument) {
    Ps2LogSpool *s = argument;
    Ps2LogIoResult r;
    lock(s);
    while (s->state.start_pending && !s->state.error_stage)
        if (wait_locked(s)) break;
    if (s->state.error_stage) { worker_done(s); unlock(s); return; }
    s->state.sink_call_active = PS2_LOG_STAGE_OPEN;
    unlock(s);
    r = s->config.ops.open(s->config.ops.sink);
    lock(s);
    s->state.sink_call_active = 0;
    if (io_result(s, r, PS2_LOG_STAGE_OPEN)) { worker_done(s); unlock(s); return; }
    s->state.sink_opened = 1;
    notify(s);
    for (;;) {
        while (!s->state.queue_used && !s->state.sealed && !s->state.error_stage)
            if (wait_locked(s)) break;
        if (s->state.error_stage) { worker_done(s); unlock(s); return; }
        if (!s->state.queue_used && s->state.sealed) break;
        s->state.inflight_bytes = s->state.inflight_offset = 0;
        s->batch_records = s->batch_last_sequence = 0;
        while (s->state.queue_used) {
            uint64_t h[2];
            peek_header(s, h);
            if (h[0] > s->config.max_record_bytes || h[0] > SIZE_MAX - HEADER ||
                HEADER + (size_t)h[0] > s->state.queue_used) {
                fail(s, PS2_LOG_STAGE_CONTRACT, CONTRACT_CODE, 1); break;
            }
            if ((size_t)h[0] > s->config.batch_bytes - s->state.inflight_bytes) break;
            queue_copy(s, h, 0, HEADER, 0);
            queue_copy(s, (unsigned char *)s->config.batch + s->state.inflight_bytes, 0, (size_t)h[0], 0);
            s->state.queue_used -= HEADER + (size_t)h[0];
            s->state.queued_payload_bytes -= h[0];
            s->state.inflight_bytes += (size_t)h[0];
            ++s->batch_records;
            s->batch_last_sequence = h[1];
        }
        notify(s); /* Queue bytes are now held by the separate inflight batch. */
        if (s->state.error_stage) { worker_done(s); unlock(s); return; }
        while (s->state.inflight_offset < s->state.inflight_bytes) {
            const size_t at = s->state.inflight_offset;
            const size_t n = s->state.inflight_bytes - at;
            s->state.sink_call_active = PS2_LOG_STAGE_WRITE;
            ++s->state.write_calls;
            unlock(s);
            r = s->config.ops.write(s->config.ops.sink, (unsigned char *)s->config.batch + at, n);
            lock(s);
            s->state.sink_call_active = 0;
            if (r.state == PS2_LOG_IO_UNCERTAIN || r.state > PS2_LOG_IO_UNCERTAIN || r.bytes > n) {
                fail(s, PS2_LOG_STAGE_WRITE, r.code, 1);
            } else {
                /* Only explicit known progress may advance the durable sink
                 * transfer offset. Flush durability is a separate receipt. */
                s->state.inflight_offset += r.bytes;
                s->state.written_bytes += r.bytes;
                if (r.state != PS2_LOG_IO_OK || r.code || !r.bytes)
                    fail(s, PS2_LOG_STAGE_WRITE, r.code, 0);
                else if (r.bytes < n) ++s->state.short_writes;
            }
            if (s->state.error_stage) { worker_done(s); unlock(s); return; }
        }
        s->state.written_records += s->batch_records;
        s->state.last_written_sequence = s->batch_last_sequence;
        s->state.inflight_bytes = s->state.inflight_offset = 0;
        s->batch_records = 0;
        refresh(s);
        notify(s);
    }
    refresh(s);
    s->state.sink_call_active = PS2_LOG_STAGE_FLUSH;
    unlock(s);
    r = s->config.ops.flush(s->config.ops.sink);
    lock(s);
    s->state.sink_call_active = 0;
    if (io_result(s, r, PS2_LOG_STAGE_FLUSH)) { worker_done(s); unlock(s); return; }
    s->state.flushed = 1;
    s->state.sink_call_active = PS2_LOG_STAGE_CLOSE;
    unlock(s);
    r = s->config.ops.close(s->config.ops.sink);
    lock(s);
    s->state.sink_call_active = 0;
    if (!io_result(s, r, PS2_LOG_STAGE_CLOSE)) s->state.closed = 1;
    worker_done(s);
    unlock(s);
}

int ps2_log_spool_start(Ps2LogSpool *s, const Ps2LogSpoolConfig *c) {
    Ps2LogSpoolConfig copy;
    if (!s || !c || s->magic) return PS2_LOG_INVALID;
    copy = *c;
    if (!range(s, sizeof(*s)) || !range(copy.queue, copy.queue_bytes) ||
        !range(copy.batch, copy.batch_bytes) || copy.queue_bytes <= HEADER ||
        !copy.max_record_bytes || copy.max_record_bytes > copy.batch_bytes ||
        copy.max_record_bytes > copy.queue_bytes - HEADER ||
        overlaps(s, sizeof(*s), copy.queue, copy.queue_bytes) ||
        overlaps(s, sizeof(*s), copy.batch, copy.batch_bytes) ||
        overlaps(copy.queue, copy.queue_bytes, copy.batch, copy.batch_bytes) ||
        !copy.ops.lock || !copy.ops.unlock || !copy.ops.wait || !copy.ops.notify_all ||
        !copy.ops.start || !copy.ops.try_join || !copy.ops.open || !copy.ops.write ||
        !copy.ops.flush || !copy.ops.close) return PS2_LOG_INVALID;
    memset(s, 0, sizeof(*s));
    s->config = copy;
    s->magic = MAGIC;
    s->state.start_pending = 1;
    const uint32_t result = copy.ops.start(copy.ops.thread, worker, s);
    lock(s);
    s->state.start_pending = 0;
    if (result == PS2_LOG_THREAD_OK) s->state.started = 1;
    else {
        if (result == PS2_LOG_THREAD_REJECTED) s->state.never_started = 1;
        else s->thread_uncertain = 1;
        fail(s, PS2_LOG_STAGE_START, result, result != PS2_LOG_THREAD_REJECTED);
    }
    refresh(s);
    notify(s);
    unlock(s);
    return result == PS2_LOG_THREAD_OK ? PS2_LOG_OK : PS2_LOG_FAILED;
}

int ps2_log_spool_append(Ps2LogSpool *s, const void *data, size_t n, uint64_t *sequence) {
    if (!s || s->magic != MAGIC) return PS2_LOG_INVALID;
    if (sequence && (!range(sequence, sizeof(*sequence)) ||
        overlaps(sequence, sizeof(*sequence), s, sizeof(*s)) ||
        overlaps(sequence, sizeof(*sequence), s->config.queue, s->config.queue_bytes) ||
        overlaps(sequence, sizeof(*sequence), s->config.batch, s->config.batch_bytes) ||
        (n && range(data, n) && overlaps(sequence, sizeof(*sequence), data, n)))) return PS2_LOG_INVALID;
    if (sequence) *sequence = 0;
    if ((!data && n) ||
        (n && (!range(data, n) || overlaps(data, n, s, sizeof(*s)) ||
         overlaps(data, n, s->config.queue, s->config.queue_bytes) ||
         overlaps(data, n, s->config.batch, s->config.batch_bytes)))) return PS2_LOG_INVALID;
    if (n > s->config.max_record_bytes) return PS2_LOG_TOO_LARGE;
    lock(s);
    ++s->state.active_producers;
    int result = PS2_LOG_OK;
    while (!s->state.sealed && !s->state.error_stage && !s->state.start_pending &&
           HEADER + n > s->config.queue_bytes - s->state.queue_used) {
        ++s->state.waiting_producers;
        ++s->state.full_waits;
        const int failed = wait_locked(s);
        --s->state.waiting_producers;
        if (failed) break;
    }
    if (s->state.error_stage || !s->state.started || s->state.start_pending) result = PS2_LOG_FAILED;
    else if (s->state.sealed) { ++s->state.rejected_after_seal; result = PS2_LOG_SEALED; }
    else if (s->state.accepted_records == UINT64_MAX || n > UINT64_MAX - s->state.accepted_bytes) {
        fail(s, PS2_LOG_STAGE_CONTRACT, CONTRACT_CODE, 0); result = PS2_LOG_FAILED;
    } else {
        uint64_t h[2] = {n, ++s->state.accepted_records};
        queue_copy(s, 0, h, HEADER, 1);
        queue_copy(s, 0, data, n, 1);
        s->state.queue_used += HEADER + n;
        s->state.queued_payload_bytes += n;
        s->state.accepted_bytes += n;
        s->state.last_accepted_sequence = h[1];
        if (s->state.queue_used > s->state.queue_high_water) s->state.queue_high_water = s->state.queue_used;
        if (sequence) *sequence = h[1];
        notify(s);
    }
    --s->state.active_producers;
    refresh(s);
    notify(s);
    unlock(s);
    return result;
}

int ps2_log_spool_seal(Ps2LogSpool *s) {
    if (!s || s->magic != MAGIC) return PS2_LOG_INVALID;
    lock(s);
    s->state.sealed = 1;
    refresh(s);
    notify(s);
    unlock(s);
    return PS2_LOG_OK;
}

int ps2_log_spool_try_join(Ps2LogSpool *s) {
    if (!s || s->magic != MAGIC) return PS2_LOG_INVALID;
    lock(s);
    if (s->state.joined || s->state.never_started) { refresh(s); unlock(s); return PS2_LOG_OK; }
    if (s->join_attempted || s->thread_uncertain) { unlock(s); return PS2_LOG_FAILED; }
    if (!s->state.sealed || s->state.start_pending || !s->state.started ||
        !s->state.worker_done || s->join_in_progress) { unlock(s); return PS2_LOG_BUSY; }
    s->join_in_progress = 1;
    unlock(s);
    const uint32_t result = s->config.ops.try_join(s->config.ops.thread);
    lock(s);
    s->join_in_progress = 0;
    if (result == PS2_LOG_THREAD_OK) s->state.joined = 1;
    else if (result != PS2_LOG_THREAD_BUSY) {
        s->join_attempted = 1;
        if (result != PS2_LOG_THREAD_REJECTED) s->thread_uncertain = 1;
        fail(s, PS2_LOG_STAGE_JOIN, result, result != PS2_LOG_THREAD_REJECTED);
    }
    refresh(s);
    unlock(s);
    return result == PS2_LOG_THREAD_OK ? PS2_LOG_OK :
        result == PS2_LOG_THREAD_BUSY ? PS2_LOG_BUSY : PS2_LOG_FAILED;
}

int ps2_log_spool_snapshot(Ps2LogSpool *s, Ps2LogSpoolSnapshot *out) {
    if (!s || s->magic != MAGIC || !range(out, sizeof(*out)) ||
        overlaps(out, sizeof(*out), s, sizeof(*s)) ||
        overlaps(out, sizeof(*out), s->config.queue, s->config.queue_bytes) ||
        overlaps(out, sizeof(*out), s->config.batch, s->config.batch_bytes)) return PS2_LOG_INVALID;
    lock(s);
    refresh(s);
    *out = s->state;
    unlock(s);
    return PS2_LOG_OK;
}
