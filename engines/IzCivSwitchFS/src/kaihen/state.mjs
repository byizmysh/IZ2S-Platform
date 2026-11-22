// kaiHEN port — shared stage state, arena, and logging.
//
// One context object flows through every stage. A stage ledger on
// globalThis.__KH keeps completed work discoverable across payload sends on
// the same boot; every stage revalidates its live state before it is skipped.

import runtime from "../core/runtime.mjs";

const {log, allocPage, malloc, free, call_rop, read8, read32, read64,
    write8, write32, write64, read_bytes, write_bytes, closeHandle,
    GADGET_MEMSET, waitMs} = runtime;

export const RO_TITLE = 0x0400000000000037n;   // system RO title (launch + donor policy)
export const HEN_TITLE = 0x040000000000003an;  // HEN title (bootstrap's own launch)

export {SD_MANIFEST} from "../iz2s-runtime.mjs";

// 30 ms per line, the emitStage cadence: the visual logger paints one
// frame per yielded line, so the whole chain stays visible on the
// untethered screen (raw unpaced log bursts append DOM rows that never
// present — the whole kaiHEN chain used to run invisible after the
// paced stage lines ended; vsdump 2026-11-16).
const KH_LOG_PAUSE_MS = 30;

export async function khLog(message) {
    await log("IZ2SCiv: " + message);
    await waitMs(KH_LOG_PAUSE_MS);
}

export function hex(value, width = 0) {
    return "0x" + BigInt(value).toString(16).toUpperCase().padStart(width, "0");
}

export function check(ok, message) {
    if (!ok) throw new Error("KH: " + message);
}

// -- Arena ------------------------------------------------------------------
// One persistent allocation for packets, path buffers, and small captures.
// Everything malloc'd here lives until the payload chain finishes.

const ARENA_SIZE = 0x180000; // 1.5 MiB

export function ensureArena(context) {
    if (context.arena) return context.arena;
    loadLedger(); // status mode skips createContext; the ledger must exist
    const base = malloc(ARENA_SIZE);
    check(Number(base) !== 0 && (base & 0xfffn) === 0n,
        "arena allocation failed or is not page aligned: " + base);
    call_rop(GADGET_MEMSET, base, 0n, BigInt(ARENA_SIZE));
    context.arena = {base: Number(base), size: ARENA_SIZE, next: 0x1000};
    globalThis.__KH.arena = {base: Number(base), size: ARENA_SIZE};
    return context.arena;
}

// Sub-allocate a page-aligned block of at least `bytes`.
export function arenaAlloc(context, bytes, name = "block") {
    const arena = ensureArena(context);
    const pages = Math.ceil(bytes / 0x1000);
    const at = arena.next;
    check(at + pages * 0x1000 <= arena.size,
        "arena exhausted at " + name + " (" + bytes + " bytes)");
    arena.next += pages * 0x1000;
    const address = arena.base + at;
    call_rop(GADGET_MEMSET, BigInt(address), 0n, BigInt(pages * 0x1000));
    return BigInt(address);
}

// Named fixed arena slots used across stages (stable addresses).
export function arenaSlots(context) {
    if (context.slots) return context.slots;
    const pkt = arenaAlloc(context, 0x2000, "packet");
    const pkt2 = arenaAlloc(context, 0x2000, "packet2");
    const path = arenaAlloc(context, 0x1000, "path");
    const data = arenaAlloc(context, 0x10000, "data"); // 64 KiB staging
    const info = arenaAlloc(context, 0x2000, "codeinfo");
    context.slots = {pkt, pkt2, path, data, info};
    return context.slots;
}

// -- Byte helpers ------------------------------------------------------------

export function bytesToHex(bytes) {
    let out = "";
    for (let i = 0; i < bytes.length; i++)
        out += (bytes[i] < 16 ? "0" : "") + bytes[i].toString(16);
    return out;
}

export function bytesEqual(a, b) {
    if (!Array.isArray(a) || !Array.isArray(b) || a.length !== b.length) return false;
    for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return false;
    return true;
}

export function u64Bytes(value) {
    const out = [];
    let v = BigInt(value);
    for (let i = 0; i < 8; i++) { out.push(Number(v & 0xffn)); v >>= 8n; }
    return out;
}

export function readU64LE(bytes, offset) {
    let value = 0n;
    for (let i = 7; i >= 0; i--)
        value = (value << 8n) | BigInt(bytes[offset + i]);
    return value;
}

export function writeU64LE(bytes, offset, value) {
    let v = BigInt(value);
    for (let i = 0; i < 8; i++) { bytes[offset + i] = Number(v & 0xffn); v >>= 8n; }
}

export function readU32LE(bytes, offset) {
    return ((bytes[offset] | bytes[offset + 1] << 8 |
        bytes[offset + 2] << 16 | bytes[offset + 3] << 24) >>> 0);
}

// Read `length` bytes at a native address into a JS array.
export function readNative(address, length) {
    return read_bytes(BigInt(address), length);
}

// Write a JS byte array to a native address.
export function writeNative(address, bytes) {
    write_bytes(BigInt(address), bytes);
}

// Verify SDK code at sdk+offset against expected hex bytes (game side).
export function verifySdkCode(offset, expectedHex, label) {
    const bytes = readNative(sdk_base + BigInt(offset), expectedHex.length / 2);
    if (bytesToHex(bytes) !== expectedHex.toLowerCase())
        throw new Error("KH: " + label + " instructions differ at sdk+" +
            offset.toString(16));
    return true;
}

// -- Handle bookkeeping -------------------------------------------------------

export function track(context, handle, label) {
    handle = Number(handle) >>> 0;
    check(handle > 0 && handle < 0xffff8000, "invalid handle from " + label);
    context.handles.push({handle, label});
    // Survive the eval: a failed run leaks FS-side sessions otherwise (two
    // leaked pm:dmnt lookups exhausted the service and a third run failed
    // with sm 0x615 on the same boot). The next payload run closes leftovers
    // from globalThis before opening anything new.
    const leaked = globalThis.__KH_TRACKED || (globalThis.__KH_TRACKED = []);
    leaked.push({handle, label});
    return handle;
}

export async function closeTracked(context, handle, label) {
    const rc = Number(closeHandle(Number(handle))) >>> 0;
    await khLog("close " + (label || "handle") + " " + hex(handle) + " rc=" + hex(rc));
    return rc;
}

// -- Ledger -------------------------------------------------------------------

export function loadLedger() {
    if (!globalThis.__KH) globalThis.__KH = {stages: {}, log: []};
    return globalThis.__KH;
}

export function stageDone(name, proof) {
    const ledger = loadLedger();
    ledger.stages[name] = {proof, at: Date.now()};
    return ledger.stages[name];
}

export function stageRecord(name) {
    return globalThis.__KH && globalThis.__KH.stages[name] ?
        globalThis.__KH.stages[name].proof : null;
}

// -- Context ------------------------------------------------------------------

export function createContext() {
    loadLedger();
    const state = globalThis.fsKaihen;
    check(state && state.domainSession && state.candidateHandle &&
        state.endpoint && state.domainLayout,
        "globalThis.fsKaihen is missing; run the main CivSwitchFS payload first");
    const context = {
        state,
        fspSrv: {handle: state.domainSession.handle, rootObj: state.domainSession.rootObj},
        candidateHandle: state.candidateHandle,
        handles: [],
        stages: {}
    };
    return context;
}
