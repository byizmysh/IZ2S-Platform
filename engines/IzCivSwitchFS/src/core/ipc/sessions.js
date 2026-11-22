// Session helpers - open fsp-srv sessions and convert them to domain sessions.

// -- Session management -------------------------------------------

// Open a fresh fsp-srv session via Service Manager + SetCurrentProcess.
// Returns the session handle, or 0 on failure.
// Caller must closeHandle() when done.
function openFspSession() {
    call_rop(SM_Initialize);
    var name = allocRawTracked(16);
    writeStr(name, "fsp-srv");
    var out = allocRawTracked(16);
    write64(out, 0n);
    var rc = call_rop(SM_GetService, out, name);
    call_rop(SM_Finalize);
    if (rc !== 0n) return 0;
    var handle = Number(read32(out)) >>> 0;
    if (!handle) return 0;
    // SetCurrentProcess (cmd 1, send_pid=true in special header)
    var buf = allocPage(0x2000);
    prepPacket(buf, 4, (10 | 0x80000000) >>> 0, []);
    write32(buf + 0x08n, 1n);
    write64(buf + 0x0Cn, 0n);
    write32(buf + 0x20n, 0x49434653n);
    write32(buf + 0x28n, 1n);
    sendIPC(handle, buf);
    var reply = cmifReply(buf);
    if (reply.result !== 0) { closeHandle(handle); return 0; }
    return handle;
}

// Clone a domain object to get our own non-domain handle.
// Sends Control cmd=1 (CopyFromCurrentDomain) on the session.
// Returns the new handle (u32), or 0 on failure.
function cloneObject(sessionHandle, domainObjId) {
    const buf = allocPage(0x2000);
    prepPacket(buf, 5, 9, [
        [16, 0x49434653],    // SFCI magic
        [24, 1],             // cmd = CopyFromCurrentDomain
        [32, domainObjId]    // which domain object to clone
    ]);
    sendIPC(sessionHandle, buf);
    const reply = cmifReply(buf);
    if (reply.result !== 0 || reply.moves.length === 0) return 0;
    return reply.moves[0];
}

// Open a domain fsp-srv session: SM -> fsp-srv -> SetCurrentProcess -> ConvertToDomain.
// Returns { handle, rootObj } or null on failure.
// Caller must closeHandle(result.handle) when done.
function openDomainSession() {
    var h = openFspSession();
    if (!h) return null;
    var buf = allocPage(0x2000);
    prepPacket(buf, 5, 8, []);             // Control type=5, 8 raw words
    write32(buf + 0x10n, 0x49434653n);     // SFCI magic
    write32(buf + 0x18n, 0n);              // cmd 0 = ConvertCurrentObjectToDomain
    sendIPC(h, buf);
    var magic = Number(read32(buf + 0x10n)) >>> 0;
    if (magic !== 0x4F434653) { closeHandle(h); return null; }
    var reply = cmifReply(buf);
    if (reply.result !== 0) { closeHandle(h); return null; }
    var root = Number(read32(buf + BigInt(reply.data))) >>> 0;
    return { handle: h, rootObj: root };
}
