// kaiHEN port — CMIF request builders and senders beyond the simple helpers
// the core already provides (multiple descriptors, PID headers, handle copies,
// domain routing, TIPC).

import runtime from "../core/runtime.mjs";
import {check, khLog, hex, readNative, writeNative, track, bytesToHex}
    from "./state.mjs";

const {sendIPC, cmifReply, closeHandle, call_rop} = runtime;
// Build one CMIF request into `pkt` (a native page) without sending it.
// See cmifSend for the options.
export function buildCmif(pkt, options) {
    const input = options.input || [];
    const xList = options.x || [], aList = options.a || [], bList = options.b || [];
    const copies = options.copies || [];
    const special = options.pid || copies.length > 0;

    for (let i = 0n; i < 0x200n; i += 8n) runtime.write64(pkt + i, 0n);

    const w0 = (options.control ? 5 : 4) |
        (xList.length << 16) | (aList.length << 20) | (bList.length << 24);
    const payloadWords = Math.ceil((32 + input.length) / 4) +
        (options.domain !== undefined ? 4 : 0);
    const w1 = (options.rawWords || payloadWords) | (special ? 0x80000000 : 0);
    runtime.write32(pkt, BigInt(w0 >>> 0));
    runtime.write32(pkt + 4n, BigInt(w1 >>> 0));

    let at = 8;
    if (special) {
        runtime.write32(pkt + BigInt(at),
            BigInt(((options.pid ? 1 : 0) | (copies.length << 1)) >>> 0));
        at += 4;
        if (options.pid) at += 8;
        for (const copy of copies) {
            runtime.write32(pkt + BigInt(at), BigInt(copy >>> 0));
            at += 4;
        }
    }
    for (const descriptor of xList) {
        const address = BigInt(descriptor.addr);
        const word0 = ((Number(descriptor.size) << 16) |
            (Number((address >> 36n) & 7n) << 6) |
            (Number((address >> 32n) & 15n) << 12) |
            (descriptor.index || 0)) >>> 0;
        runtime.write32(pkt + BigInt(at), BigInt(word0));
        runtime.write32(pkt + BigInt(at + 4),
            BigInt(Number(address & 0xffffffffn)));
        at += 8;
    }
    for (const descriptor of [...aList, ...bList]) {
        const address = BigInt(descriptor.addr);
        const size = BigInt(descriptor.size);
        runtime.write32(pkt + BigInt(at), BigInt(Number(size & 0xffffffffn)));
        runtime.write32(pkt + BigInt(at + 4),
            BigInt(Number(address & 0xffffffffn)));
        // Attribute parity with the author's recorded engine (files.cjs):
        // BOTH send and receive data buffers carry mode 1 (NonSecureIpc) —
        // their donor writes (send, line 177) and reads (receive, lines
        // 125/185) all use it. Empirically (runs 74-81): attr 0 or 3 on
        // data buffers gets the session killed; attr 1 works everywhere
        // it has been tried (WithContext npdm read, plain SD read). The
        // one exception is the small 0x1a4 code-info receive buffer, which
        // the recording sends as attr 0 and which works attr 0.
        const mode = 1n;
        const word2 = (mode | ((address >> 36n) & 7n) << 2n |
            ((size >> 32n) & 0xfn) << 24n |
            ((address >> 32n) & 15n) << 28n);
        runtime.write32(pkt + BigInt(at + 8), BigInt(Number(word2)));
        at += 12;
    }
    at = (at + 15) & ~15;

    if (options.domain !== undefined) {
        const sfciPayload = 16 + input.length;
        runtime.write32(pkt + BigInt(at), BigInt((1 | (sfciPayload << 16)) >>> 0));
        runtime.write32(pkt + BigInt(at + 4), BigInt(options.domain >>> 0));
        at += 16;
    }
    runtime.write32(pkt + BigInt(at), 0x49434653n);           // SFCI
    runtime.write32(pkt + BigInt(at + 8), BigInt(options.cmd));
    const dataAt = at + 16;
    if (options.inputAt) options.inputAt(pkt + BigInt(dataAt));
    else if (input.length) writeNative(pkt + BigInt(dataAt), input);
    return dataAt;
}

// The game SDK dispatcher (sendIPC, sdk+0x14ba90) rejected one request shape
// with a bogus non-Result transport word (OpenCodeFileSystem on the copied
// Loader root: 0x25A0B — every other shape transported fine). The reference
// sends everything through the bare svc #0x22 wrapper instead (civ7
// sdk-functions.json "send": sdk+0x168C54, 41 04 00 d4 c0 03 5f d6 =
// SendSyncRequestWithUserBuffer(handle, buffer, size) ; ret). Retry through it
// when the dispatcher refuses, verify its bytes live first, and log both codes.
const BARE_SEND_OFFSET = 0x168C54;
const BARE_SEND_BYTES = "410400d4c0035fd6";

export function bareSendTransport(handle, pkt) {
    const bytes = readNative(sdk_base + BigInt(BARE_SEND_OFFSET), 8);
    check(bytesToHex(bytes) === BARE_SEND_BYTES,
        "bare svc send wrapper differs at sdk+" +
            BARE_SEND_OFFSET.toString(16) + ": " + bytesToHex(bytes));
    // S2 svc #0x22 (SendSyncRequestWithUserBuffer) argument order is
    // (buffer, size, handle) — verified live on the console (probe-swapped:
    // (buf,size,handle) transports 0; S1's (handle,buf,size) gets 0xCC01),
    // and it is how the reference calls it (session.cjs call('send', base,
    // 4096, handle) -> svc #0x22; ret at sdk+0x168C54).
    return Number(call_rop(sdk_base + BigInt(BARE_SEND_OFFSET),
        pkt, 0x1000n, BigInt(handle))) >>> 0;
}

// Build and send one CMIF request. Returns the parsed reply.
export function cmifSend(handle, options) {
    const pkt = options.pkt;
    buildCmif(pkt, options);
    let transport = Number(sendIPC(handle, pkt)) >>> 0;
    if (transport !== 0) {
        const dispatcherCode = transport;
        transport = bareSendTransport(handle, pkt);
        khLog("transport fallback handle=" + hex(handle) +
            " dispatcher=" + hex(dispatcherCode) + " bare=" + hex(transport));
    }
    const reply = {transport, buf: pkt};
    if (transport !== 0) return reply;
    const parsed = cmifReply(pkt);
    reply.result = parsed.result;
    reply.copies = parsed.copies;
    reply.moves = parsed.moves;
    reply.dataAt = parsed.data;
    reply.at = parsed.at;
    if (options.domain !== undefined) {
        // Domain reply: object count at the aligned payload start, SFCO
        // result at +24, data at +32, and the new object ids AFTER the
        // command's response data (use readDomainObjects with the data size).
        const domainAt = parsed.at;
        reply.objCount = Number(runtime.read32(pkt + BigInt(domainAt))) >>> 0;
        reply.domainDataAt = domainAt + 32;
        reply.result = Number(runtime.read32(pkt + BigInt(domainAt + 24))) >>> 0;
        reply.dataAt = domainAt + 32;
    }
    return reply;
}

// Read new domain object ids from a domain reply buffer; dataSize is the
// command's response payload size before the id list.
export function readDomainObjects(buf, objCount, dataSize) {
    const ids = [];
    const off = Number(buf) + 0x30 + (dataSize || 0);
    for (let i = 0; i < objCount && i < 8; i++)
        ids.push(runtime.read32(BigInt(off + i * 4)) >>> 0);
    return ids;
}

export function requireOk(reply, label) {
    check(reply.transport === 0, label + ": transport failed " + hex(reply.transport));
    check(reply.result === 0, label + ": result " + hex(reply.result));
    return reply;
}

// Canonical module of a Result (the low 22 bits name the module+description).
export function canonical(result) {
    return (Number(result) >>> 0) & 0x3fffff;
}

export function oneMovedHandle(reply, label) {
    requireOk(reply, label);
    check(reply.moves.length === 1 && reply.copies.length === 0,
        label + ": expected exactly one moved handle (moves=" +
        reply.moves.length + " copies=" + reply.copies.length + ")");
    return reply.moves[0];
}

// TIPC request on the FS process's sm: session (reference 'sm-get' worker).
// Request: {u32 type=0x11, u32 token=2, name bytes at +8}.
// Reply: {u32 0x11, u32 0x80000001, u32 0x20, u32 handle@12, u32 status@16}.
export function tipcGetService(handle, name, pkt) {
    for (let i = 0n; i < 0x80n; i += 8n) runtime.write64(pkt + i, 0n);
    runtime.write32(pkt, 0x11n);
    runtime.write32(pkt + 4n, 2n);
    for (let i = 0; i < name.length; i++)
        runtime.write8(pkt + BigInt(8 + i), BigInt(name.charCodeAt(i)));
    const transport = Number(sendIPC(handle, pkt)) >>> 0;
    check(transport === 0, "TIPC sm: '" + name + "' transport " + hex(transport));
    const raw = readNative(pkt, 20);
    const word = offset =>
        (raw[offset] | raw[offset + 1] << 8 | raw[offset + 2] << 16 |
            raw[offset + 3] << 24) >>> 0;
    check(word(0) === 0x11 && word(4) === 0x80000001 && word(8) === 0x20,
        "unexpected TIPC sm reply for '" + name + "': " +
        [word(0), word(4), word(8)].map(hex).join(","));
    const status = word(16);
    const service = word(12);
    if (status !== 0) return {result: status, handle: 0};
    check(service > 0 && service < 0xffff8000,
        "TIPC sm: '" + name + "' returned no session handle");
    return {result: 0, handle: service};
}
