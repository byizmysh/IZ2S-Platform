// kaiHEN port — PM access through the FS process's sm: session: export the
// session, look up pm:shell / pm:dmnt as FS, query the live RO process id,
// and the small syscall stubs (thread id, wait, cancel, event signal/clear).

import runtime from "../core/runtime.mjs";
import {check, khLog, hex, u64Bytes, readU64LE, readU32LE, readNative,
    bytesToHex, track, verifySdkCode} from "./state.mjs";
import {readFs, fsPointer} from "./endpoint.mjs";
import {cmifSend, tipcGetService, requireOk} from "./wire.mjs";
import {exportHandle} from "./objects.mjs";
import {arenaSlots} from "./state.mjs";

const {call_rop, allocPage} = runtime;

// target.json nativeFunctions (game SDK offsets, decimal as in the source).
export const NATIVE = {
    cancelSynchronization: 1477644,
    eventSyscallScan: 1477376,
    threadId: 1477748,
    waitZero: 1477620,
    processInfo: 1477796
};

// Out-parameter svc wrappers share one template (reference ro/control.cjs
// pins the full 24 bytes per wrapper; civ7 SDK dump matches — RESIDUAL §2):
//   str x0,[sp,#-0x10]! ; svc #N ; ldr x2,[sp] ; str x1/w1,[x2] ; add sp,0x10 ; ret
// threadId stores the 64-bit id (str x1, 410000f9); waitZero stores w1
// (410000b9); processInfo keeps the 64-bit store with svc #0x51.
const OUT_WRAPPER_TEMPLATES = {
    threadId: {offset: NATIVE.threadId,
        bytes: "e00f1ff8a10400d4e20340f9410000f9ff430091c0035fd6"},
    waitZero: {offset: NATIVE.waitZero,
        bytes: "e00f1ff8010300d4e20340f9410000b9ff430091c0035fd6"},
    processInfo: {offset: NATIVE.processInfo,
        bytes: "e00f1ff8210500d4e20340f9410000f9ff430091c0035fd6"}
};

function verifyOutWrapper(name) {
    const wrapper = OUT_WRAPPER_TEMPLATES[name];
    verifySdkCode(wrapper.offset, wrapper.bytes, name + " wrapper");
}

// Bare svc wrappers are: svc #N ; ret (8 bytes).
const CANCEL_BYTES = "210300d4c0035fd6";
const EVENT_SIGNAL_BYTES = "210200d4c0035fd6";  // svc #0x11 SignalEvent
const EVENT_CLEAR_BYTES = "410200d4c0035fd6";    // svc #0x12 ClearEvent

export function verifyNativeStubs(context) {
    verifyOutWrapper("threadId");
    verifyOutWrapper("waitZero");
    // cancelSynchronization: bare svc #0x19; ret (reference worker-control
    // cancelCode fallback bytes).
    verifySdkCode(NATIVE.cancelSynchronization, CANCEL_BYTES,
        "cancelSynchronization stub");
    // Signal/clear candidates in the scan region; identity is proven by the
    // prepareGateEvent qualification, which also swaps them if needed.
    const range = readNative(sdk_base + BigInt(NATIVE.eventSyscallScan), 0x120);
    let hexString = "";
    for (const byte of range) hexString += (byte < 16 ? "0" : "") + byte.toString(16);
    const signalAt = hexString.indexOf(EVENT_SIGNAL_BYTES);
    const clearAt = hexString.indexOf(EVENT_CLEAR_BYTES);
    check(signalAt >= 0 && signalAt % 8 === 0 &&
        signalAt === hexString.lastIndexOf(EVENT_SIGNAL_BYTES),
        "native event signal wrapper is not unique");
    check(clearAt >= 0 && clearAt % 8 === 0 &&
        clearAt === hexString.lastIndexOf(EVENT_CLEAR_BYTES),
        "native event clear wrapper is not unique");
    context.eventStub = {
        signal: NATIVE.eventSyscallScan + signalAt / 2,
        clear: NATIVE.eventSyscallScan + clearAt / 2
    };
    return context.eventStub;
}

// After the gate event exists, prove which scanned wrapper signals and which
// clears: clear → not signaled, signal → signaled, clear again. Swap the
// offsets if the qualification fails on the first assignment.
export function qualifyEventStubs(context, event) {
    function qualifies() {
        return eventClear(context, event.read) === 0 &&
            waitZero(event.read) === 0xea01 &&
            eventSignal(context, event.write) === 0 &&
            waitZero(event.read) === 0 &&
            eventClear(context, event.read) === 0 &&
            waitZero(event.read) === 0xea01;
    }
    if (qualifies()) return context.eventStub;
    const stub = context.eventStub;
    context.eventStub = {signal: stub.clear, clear: stub.signal};
    check(qualifies(), "neither event stub assignment qualifies as signal/clear");
    return context.eventStub;
}

// Export the FS process's sm: client session and look up pm:shell + pm:dmnt.
export async function openPmSessions(context, roTitle) {
    const address = context.layout.sdk + 4768; // smServiceOffset
    const before = await readFs(context, address - 4, 16);
    const handle = readU32LE(before, 4);
    const initialized = readU32LE(before, 8) === 0 &&
        (before[12] | before[13] << 8) === 0x101 &&
        await fsPointer(context, address - 8) === address;
    check(initialized && handle >= 0x8000 && handle < 0x2000000,
        "FS sm: session candidate is not initialized (handle=" + hex(handle) + ")");
    const exported = await exportHandle(context, handle, "FS sm session");

    const slots = arenaSlots(context);
    const shellLookup = tipcGetService(exported.handle, "pm:shell", slots.pkt);
    check(shellLookup.result === 0 && shellLookup.handle,
        "pm:shell lookup failed: " + hex(shellLookup.result));
    const monitorLookup = tipcGetService(exported.handle, "pm:dmnt", slots.pkt);
    check(monitorLookup.result === 0 && monitorLookup.handle,
        "pm:dmnt lookup failed: " + hex(monitorLookup.result));

    // pm:shell command 12: the process id currently registered for the title.
    const query = cmifSend(shellLookup.handle, {
        pkt: slots.pkt2, cmd: 12, input: u64Bytes(BigInt(roTitle)), rawWords: 10
    });
    requireOk(query, "pm identity query");
    const originalPid = readU64LE(readNative(slots.pkt2 + BigInt(query.dataAt), 8), 0);
    check(originalPid > 1n && originalPid < 0x1000000n,
        "implausible original RO pid " + originalPid);

    context.pm = {
        sm: exported.handle, shell: shellLookup.handle,
        monitor: monitorLookup.handle, originalPid: Number(originalPid)
    };
    track(context, shellLookup.handle, "pm:shell");
    track(context, monitorLookup.handle, "pm:dmnt");
    await khLog("pm ready shell=" + hex(shellLookup.handle) +
        " dmnt=" + hex(monitorLookup.handle) +
        " original RO pid=" + originalPid.toString());
    return context.pm;
}

// A plain IPC on a PM session with a u64 argument (reference root-native-ipc).
export function pmIpcU64(context, sessionHandle, cmd, value, label) {
    const slots = arenaSlots(context);
    const reply = cmifSend(sessionHandle,
        {pkt: slots.pkt, cmd, input: u64Bytes(BigInt(value)), rawWords: 10});
    check(reply.transport === 0, label + ": transport " + hex(reply.transport));
    return {result: reply.result,
        data: readNative(slots.pkt + BigInt(reply.dataAt), 16), reply};
}

// Call a stub that writes a u64 into an output word (threadId / waitZero).
export function stubCallU64(offset, out, ...args) {
    const rc = call_rop(sdk_base + BigInt(offset), out, ...args.map(BigInt));
    return {rc: Number(rc) >>> 0, value: runtime.read64(out)};
}

// svcCancelSynchronization on a thread handle.
export function cancelThread(handle) {
    const rc = call_rop(sdk_base + BigInt(NATIVE.cancelSynchronization),
        BigInt(handle), 0n, 0n);
    return Number(rc) >>> 0;
}

// Signal / clear an event by its FS-process handle through the game stubs.
export function eventSignal(context, handle) {
    return Number(call_rop(sdk_base + BigInt(context.eventStub.signal),
        BigInt(handle), 0n, 0n)) >>> 0;
}

export function eventClear(context, handle) {
    return Number(call_rop(sdk_base + BigInt(context.eventStub.clear),
        BigInt(handle), 0n, 0n)) >>> 0;
}

// waitZero: poll one handle through the wait stub (0 = signaled, 0xea01 =
// timeout). The handle goes into the handle array at out+8.
export function waitZero(handle) {
    const out = allocPage(0x40);
    runtime.write64(out, 0n);
    runtime.write64(out + 8n, 0n);
    runtime.write32(out + 8n, BigInt(Number(handle) >>> 0));
    const rc = call_rop(sdk_base + BigInt(NATIVE.waitZero), out, out + 8n,
        1n, 0n);
    return Number(rc) >>> 0;
}
