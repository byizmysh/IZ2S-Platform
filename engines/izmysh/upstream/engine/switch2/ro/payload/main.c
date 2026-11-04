// A small native RO proof server. No runtime, heap, or absolute pointers.
typedef unsigned char u8;
typedef unsigned int u32;
typedef unsigned long long u64;
typedef long long s64;
typedef struct { u64 result, value; } Reply;

extern Reply connect_port(const char *name);
extern u32 send_request(u32 handle);
extern u32 close_handle(u32 handle);
extern void sleep_thread(s64 ns);
extern Reply wait_handles(const u32 *handles, u32 count, s64 timeout);
extern Reply reply_receive(const u32 *handles, u32 count, u32 reply, s64 timeout);
extern Reply accept_session(u32 port);
extern Reply process_id(u32 handle);
extern Reply thread_id(u32 handle);
extern u64 system_tick(void);
extern u32 process_request(u32 command, const u64 *input, u32 bytes,
    u32 handle, u64 *output, u32 *output_bytes, void *receive, u64 receive_size);

static u32 *message(void) {
    u32 *p;
    __asm__ volatile("mrs %0, tpidrro_el0" : "=r"(p));
    return p;
}

static void clear(u32 *p) {
    for (u32 i = 0; i < 64; i++) p[i] = 0;
}

static u32 register_service(u32 sm, u32 space, const char *name, u32 *last_result) {
    u32 *p = message();
    clear(p);
    p[0] = 0x12;
    p[1] = 5;
    p[2] = space;
    for (u32 i = 0; name[i] && i < 8; i++) ((u8 *)p)[12 + i] = name[i];
    p[5] = 4; // max sessions; the following bool is false (normal IPC).
    u32 transport = send_request(sm);
    if (transport) { *last_result = transport; return 0; }
    if (p[0] != 0x12 || p[1] != 0x80000001 || p[2] != 0x20) {
        *last_result = 0xffffffff;
        return 0;
    }
    *last_result = p[4];
    return p[4] ? 0 : p[3];
}

static Reply respond(u32 session, u64 registration_result) {
    u32 *p = message();
    u32 at = 2, handle = 0, valid = p[0] == 4 || p[0] == 0x01000004;
    u64 receive_address = 0, receive_size = 0;
    if (valid && p[1] >> 31) {
        u32 special = p[at++];
        // Process probes accept one copied handle and an optional kernel PID.
        valid = (special & ~1u) == 2;
        if (special & 1) at += 2;
        if (valid) handle = p[at++];
    }
    if (valid && p[0] == 0x01000004) {
        // One kernel-validated receive map-alias buffer, used only for bulk reads.
        u32 flags = p[at + 2];
        receive_size = p[at] | ((u64)((flags >> 24) & 15) << 32);
        receive_address = p[at + 1] | ((u64)(flags >> 28) << 32) |
            ((u64)((flags >> 2) & 7) << 36);
        at += 3;
    }
    u32 raw_words = p[1] & 0x3ff;
    u32 end = at + raw_words;
    at = (at + 3) & ~3u;
    valid = valid && end <= 64 && end >= at + 4 && p[at] == 0x49434653;
    u32 command = valid ? p[at + 2] : 0;
    u64 output[24] = {0};
    u32 output_bytes = 48, result = 0xf601;
    Reply pid = process_id(0xffff8001), tid = thread_id(0xffff8000);
    u64 tick = system_tick();
    if (valid && command == 0x524f0001 && !handle && end >= at + 6) {
        output[0] = 0x4558454f5232534eULL;
        output[1] = ((u64 *)(p + at + 4))[0] ^ 0x9d7c3a51e6b2048fULL;
        output[2] = pid.value; output[3] = tid.value;
        output[4] = tick; output[5] = registration_result;
        result = 0;
    } else if (valid && handle) {
        result = process_request(command, (u64 *)(p + at + 4),
            (end - at - 4) * 4, handle, output, &output_bytes,
            (void *)receive_address, receive_size);
    }
    if (handle) close_handle(handle);
    clear(p);
    p[1] = 0x80000000 | ((32 + output_bytes + 3) / 4);
    p[2] = 1; // The kernel inserts our PID into the reply descriptor.
    p[8] = 0x4f434653; // SFCO, after the special-header alignment.
    p[10] = result;
    u64 *out = (u64 *)(p + 12);
    for (u32 i = 0; i < (output_bytes + 7) / 8; i++) out[i] = output[i];
    return reply_receive((const u32 *)0, 0, session, 0);
}

__attribute__((noreturn)) void main(void) {
    Reply connected;
    do {
        connected = connect_port("sm:");
        if (connected.result) sleep_thread(50000000);
    } while (connected.result);
    u32 sm = connected.value;
    u32 *p = message();
    clear(p);
    p[0] = 0x10;
    p[1] = 0x80000000;
    p[2] = 1;
    if (send_request(sm) || p[2]) for (;;) sleep_thread(1000000000);

    u32 ports[2] = {0, 0}, sessions[4] = {0, 0, 0, 0};
    u32 registration[2] = {0, 0};
    for (;;) {
        // Native RO registers ro:1 only in space 1. Space 2 is optional here.
        if (!ports[0]) ports[0] = register_service(sm, 2, "ro:1", &registration[0]);
        // If stock RO owns this name, retain it until the controller frees it.
        if (!ports[1]) ports[1] = register_service(sm, 1, "ro:dmnt", &registration[1]);
        u32 handles[6], kinds[6], count = 0;
        for (u32 i = 0; i < 2; i++) if (ports[i]) {
            handles[count] = ports[i]; kinds[count++] = i;
        }
        for (u32 i = 0; i < 4; i++) if (sessions[i]) {
            handles[count] = sessions[i]; kinds[count++] = 2 + i;
        }
        if (!count) { sleep_thread(50000000); continue; }
        // A receive without a reply target needs a blank receive header.
        // This call waits for either a port connection or a session request.
        clear(message());
        Reply ready = reply_receive(handles, count, 0, 50000000);
        if (ready.value >= count) continue;
        u32 kind = kinds[ready.value];
        if (ready.result) {
            if (ready.result == 0xf601 && kind >= 2) {
                close_handle(sessions[kind - 2]); sessions[kind - 2] = 0;
            }
            continue;
        }
        if (kind < 2) {
            Reply accepted = accept_session(ports[kind]);
            if (accepted.result) continue;
            u32 slot = 0;
            while (slot < 4 && sessions[slot]) slot++;
            if (slot < 4) sessions[slot] = accepted.value;
            else close_handle(accepted.value);
        } else {
            u32 slot = kind - 2;
            if (message()[0] == 2) {
                close_handle(sessions[slot]); sessions[slot] = 0;
            } else {
                Reply sent = respond(sessions[slot], ((u64)registration[1] << 32) | registration[0]);
                if (sent.result == 0xf601) { close_handle(sessions[slot]); sessions[slot] = 0; }
            }
        }
    }
}
