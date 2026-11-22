// kaiHEN port — the ldr:ro frontend: open a fresh session from the game once
// kaiHEN's RO process owns the service, Initialize it, and load/unload the
// bootstrap NRO through it.

import runtime from "../core/runtime.mjs";
import {check, khLog, hex, readU64LE, readU32LE, arenaSlots, arenaAlloc,
    readNative, writeNative, writeU64LE, u64Bytes, verifySdkCode} from "./state.mjs";
import {cmifSend, buildCmif, requireOk} from "./wire.mjs";
import {readFs} from "./endpoint.mjs";

const {allocPage, call_rop, closeHandle} = runtime;

// sdk-functions.json: nn::sm GetService wrapper (out, name, len).
const SM_GET_SERVICE = 0x14cc90;

export function openService(name) {
    const nameBuf = allocPage(16);
    runtime.writeStr(nameBuf, name);
    const out = allocPage(16);
    runtime.write64(out, 0n);
    const rc = call_rop(sdk_base + BigInt(SM_GET_SERVICE), out, nameBuf,
        BigInt(name.length));
    check(Number(rc) >>> 0 === 0, "GetService('" + name + "') failed: " + hex(rc));
    const handle = Number(runtime.read32(out)) >>> 0;
    check(handle > 0 && handle < 0xffff8000,
        "'" + name + "' returned no handle");
    return handle;
}

// With send_pid, the CMIF payload begins with an 8-byte slot the kernel
// fills with the caller's pid; arguments follow it (argumentsWithPid).
const pidPad = () => [0, 0, 0, 0, 0, 0, 0, 0];

// Fresh ldr:ro session + Initialize (cmd 4, PID header, current-process copy).
export function connectRoFrontend(context) {
    const slots = arenaSlots(context);
    const handle = openService("ldr:ro");
    context.ro = {handle};
    const reply = cmifSend(handle, {
        pkt: slots.pkt, cmd: 4, input: pidPad(), pid: true,
        copies: [0xffff8001], rawWords: 10
    });
    requireOk(reply, "RO Initialize");
    context.ro.initialized = true;
    return context.ro;
}

// svcQueryMemory wrapper offset (probe_allocation_36m lineage).
const SVC_QUERY_MEMORY = 0x168b0c;

function queryMemory(address) {
    const aux = allocPage(0x40);
    const rc = call_rop(sdk_base + BigInt(SVC_QUERY_MEMORY), aux,
        aux + 0x40n, BigInt(address));
    if (Number(rc) >>> 0 !== 0) return null;
    return {
        base: runtime.read64(aux), size: runtime.read64(aux + 8n),
        type: Number(runtime.read32(aux + 0x10n)),
        permissions: Number(runtime.read32(aux + 0x18n))
    };
}

// LoadNRO (cmd 0) with PID header + current-process copy; returns destination.
function loadNro(context, source, bytesLength, bss, bssSize) {
    const slots = arenaSlots(context);
    // Cmd 0 carries NO handle copies — only Initialize (cmd 4) passes the
    // current-process copy. An extra copy on LoadNro is rejected with
    // 0x1759 (run106; the author's nro.cjs sends copies only on cmd 4).
    const reply = cmifSend(context.ro.handle, {
        pkt: slots.pkt, cmd: 0, pid: true,
        input: pidPad().concat(u64Bytes(BigInt(source)))
            .concat(u64Bytes(BigInt(bytesLength)))
            .concat(u64Bytes(BigInt(bss))).concat(u64Bytes(BigInt(bssSize))),
        rawWords: 18
    });
    requireOk(reply, "LoadNRO");
    return Number(readU64LE(readNative(slots.pkt + BigInt(reply.dataAt), 8), 0));
}

function unloadNro(context, destination) {
    const slots = arenaSlots(context);
    const reply = cmifSend(context.ro.handle, {
        pkt: slots.pkt, cmd: 1, pid: true,
        input: pidPad().concat(u64Bytes(BigInt(destination))), rawWords: 12
    });
    requireOk(reply, "UnloadNRO");
}

// RO command 4 (Initialize) is required again after a previous session closed
// per upstream behaviour; our session stays initialized for the whole run.

// Build the NRO image: header + text at 0x80 (reference makeBootstrapImage).
export function buildNroImage(text) {
    check(text.length <= 0xff80, "native bootstrap exceeds text segment");
    const image = new Array(0x12000).fill(0);
    const write = (offset, bytes) => {
        for (let i = 0; i < bytes.length; i++) image[offset + i] = bytes[i];
    };
    const u32 = (offset, value) => {
        image[offset] = value & 0xff; image[offset + 1] = value >> 8 & 0xff;
        image[offset + 2] = value >> 16 & 0xff;
        image[offset + 3] = value >> 24 & 0xff;
    };
    for (let i = 0; i < 4; i++) image[0x10 + i] = "NRO0".charCodeAt(i);
    u32(0x18, 0x12000);
    u32(0x24, 0x10000);
    u32(0x28, 0x10000);
    u32(0x2c, 0x1000);
    u32(0x30, 0x11000);
    u32(0x34, 0x1000);
    u32(0x38, 0x1000);
    write(0x80, text);
    return image;
}

// The Civ7 trywait adapter (and x0,x0,#~7; ldr x16,[pc+16]; br x16; .quad).
export function tryWaitAdapterBytes(target) {
    const words = [0x927df000, 0x58000070, 0xd61f0200, 0xd503201f];
    const bytes = [];
    for (const word of words) {
        bytes.push(word & 0xff, word >> 8 & 0xff, word >> 16 & 0xff,
            word >>> 24);
    }
    return bytes.concat(u64Bytes(BigInt(target)));
}

// Run the mapped NRO entry on an SDK pthread (the reference sdk-thread.cjs):
// pthread_create(out, NULL, entry, args) from the main thread, poll the
// untagged tryWait, then join with the tagged handle. The bootstrap needs a
// full SDK thread (TLS IPC buffers, allocator context) for its own fsp-srv
// calls and for the pthreads it spawns for HEN.
const PTHREAD_CODE = {
    create: {offset: 0x142180,
        bytes: "ff4302d1fd7b04a9f92b00f9f85f06a9f65707a9f44f08a9"},
    join: {offset: 0x142480,
        bytes: "e803202a1f0940f261020054fd7bbea9f44f01a9fd030091"},
    tryWait: {offset: 0x137110,
        bytes: "484f00f0e10300aa084141f900c10391accf1214"},
    tryWaitImplementation: {offset: 0x13e120,
        bytes: "ff0301d1fd7b01a9f51300f9f44f03a9fd43009128b041b9"}
};

export function verifyPthreadCode() {
    for (const [name, entry] of Object.entries(PTHREAD_CODE))
        verifySdkCode(entry.offset, entry.bytes, "pthread " + name);
    return PTHREAD_CODE;
}

// Map the image, run its entry on an SDK pthread with args at `argsAddress`,
// join, unload. Returns {returned, destination}.
export async function runNroOnRawThread(context, imageBytes, argsAddress,
        argsBytes, options = {}) {
    const ro = context.ro;
    check(ro && ro.initialized, "the RO frontend is not connected");
    verifyPthreadCode();

    // source = image bytes, bss = one zero page after it.
    const source = arenaAlloc(context, imageBytes.length + 0x1000, "nro-source");
    writeNative(source, imageBytes);
    const bss = source + BigInt(imageBytes.length);

    const destination = loadNro(context, source, imageBytes.length, bss, 0x1000);
    // NRO destinations can land in the low 32-bit range on S2 (run107:
    // 0x818C000); the author's nro.cjs applies no range check.
    check(destination > 0 && destination < 0x800000000000,
        "LoadNRO returned no destination: " + hex(destination));
    const base = BigInt(destination);

    // Verify segment mappings (RX/R/RW/RW at the four offsets).
    const offsets = [0, 0x10000, 0x11000, 0x12000];
    const expected = [5, 1, 3, 3];
    for (let i = 0; i < offsets.length; i++) {
        const region = queryMemory(base + BigInt(offsets[i]));
        check(region && region.permissions === expected[i],
            "mapped segment permissions differ at +" +
            offsets[i].toString(16) + " (" +
            (region ? region.permissions : "none") + " != " + expected[i] + ")");
    }

    // Place the args and let the caller patch late fields (the second
    // parameter is the mapped NRO base, for entry-relative fixups).
    writeNative(argsAddress, argsBytes);
    if (options.patchArgs) await options.patchArgs(argsAddress, destination);
    const entry = base + 0x80n;

    // pthread_create(slots, NULL, entry, args) on the main thread.
    const slots = allocPage(0x40);
    runtime.write64(slots, 0n);
    runtime.write64(slots + 8n, 0n);
    runtime.write64(slots + 16n, 0n);
    const createRc = call_rop(sdk_base + BigInt(PTHREAD_CODE.create.offset),
        slots, 0n, entry, argsAddress);
    check(Number(createRc) >>> 0 === 0,
        "pthread_create failed: " + hex(createRc));
    const thread = Number(runtime.read64(slots));
    check(Number.isSafeInteger(thread) && thread > 0 && thread < 2 ** 39 &&
        (thread & 7) === 7,
        "pthread_create returned an untagged thread: " + thread);
    const waitable = thread - 7; // tryWait needs the untagged ThreadType*

    // Poll the untagged tryWait until the bootstrap thread exits.
    const deadline = Date.now() + (options.waitMs || 90000);
    for (;;) {
        const state = Number(call_rop(
            sdk_base + BigInt(PTHREAD_CODE.tryWait.offset),
            BigInt(waitable), 0n, 0n)) >>> 0;
        check(state === 0 || state === 1, "unexpected SDK thread state: " + state);
        if (state === 1) break;
        check(Date.now() < deadline, "bootstrap thread did not exit in time");
        runtime.waitMs(10);
    }
    const returnSlot = slots + 8n;
    const joinRc = call_rop(sdk_base + BigInt(PTHREAD_CODE.join.offset),
        BigInt(thread), returnSlot, 0n);
    check(Number(joinRc) >>> 0 === 0, "pthread_join failed: " + hex(joinRc));
    const returned = Number(runtime.read64(returnSlot));

    unloadNro(context, destination);
    const unloadOffsets = [0, 0x10000, 0x11000, 0x12000];
    for (const offset of unloadOffsets) {
        const region = queryMemory(base + BigInt(offset));
        check(!region || region.permissions === 0,
            "an NRO segment remains accessible after unload at +" +
            offset.toString(16));
    }
    return {returned, destination};
}
