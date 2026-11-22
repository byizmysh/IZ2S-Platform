// kaiHEN port — native worker threads in the game: the API-worker group from
// the FS thread ring, SDK gate events, and gated IPC workers (control-test
// and pm-launch) built on CivSwitchFS's raw-thread frames.

import runtime from "../core/runtime.mjs";
import {check, khLog, hex, readU64LE, readU32LE, readNative, track,
    verifySdkCode, arenaSlots, writeNative} from "./state.mjs";
import {readFs} from "./endpoint.mjs";
import {exportHandle} from "./objects.mjs";
import {cancelThread, stubCallU64, NATIVE} from "./pm.mjs";

const {
    buildRawFrame, createRawWorker, startRawWorker, waitRawThread,
    closeRawThread, borrowThreadScheduling, allocPage, call_rop,
    readFrameResult
} = runtime;

// event-code.json (game SDK): manual-reset light events in owned memory.
export const EVENT_CODE = {
    initialize: 0x1324e0, finalize: 0x132510, wait: 0x132680,
    tryWait: 0x1327e0, signal: 0x132520, clear: 0x132640
};

const EVENT_INIT_BYTES = "280080525f04007108040039e8179f1a3f000072080000394800805208119f1a080400b9";
const EVENT_SIGNAL_BYTES = "ff0301d1fd7b01a9f65702a9f44f03a9fd430091f30300aa68464038e802003454008052";
const EVENT_CLEAR_BYTES = "08100091bf3b03d51f2003d51f2003d509fd5f883f090071a10000541ffd098889ffff35";
const EVENT_WAIT_BYTES = "ff0301d1fd7b01a9f65702a9f44f03a9fd430091f30300aa684640384805003435008052";

export function verifyEventCode() {
    verifySdkCode(EVENT_CODE.initialize, EVENT_INIT_BYTES, "event initialize");
    verifySdkCode(EVENT_CODE.signal, EVENT_SIGNAL_BYTES, "event signal");
    verifySdkCode(EVENT_CODE.clear, EVENT_CLEAR_BYTES, "event clear");
    verifySdkCode(EVENT_CODE.wait, EVENT_WAIT_BYTES, "event wait");
}

// Initialize a manual-reset SDK event in owned memory; returns its address.
export function makeEvent(context, name) {
    const address = allocPage(0x20);
    runtime.write64(address, 0n);
    runtime.write64(address + 8n, 0n);
    const rc = call_rop(sdk_base + BigInt(EVENT_CODE.initialize), address, 0n, 0n);
    // The SDK event initializer is void — its x0 return is whatever the
    // epilogue left there (the author's events.cjs ignores it too). The
    // authoritative check is the state it wrote, verified below.
    void rc;
    const bytes = readNative(address, 8);
    check(bytes[0] === 0 && bytes[1] === 1 && readU32LE(bytes, 4) === 0,
        "event " + name + " did not initialize unsignaled");
    return address;
}

export function signalEvent(address) {
    const rc = call_rop(sdk_base + BigInt(EVENT_CODE.signal), address, 0n, 0n);
    check(Number(rc) >>> 0 === 0, "event signal failed");
}

export function clearEvent(address) {
    const rc = call_rop(sdk_base + BigInt(EVENT_CODE.clear), address, 0n, 0n);
    check(Number(rc) >>> 0 === 0, "event clear failed");
}

// -- API workers ----------------------------------------------------------------

// Find the unique 7-thread API-worker group, export each thread handle into
// the game, verify its thread id, and prove cancellation works.
export async function prepareApiWorkers(context) {
    const nodes = context.threadRing;
    const groups = {};
    for (const node of nodes) {
        if (node.state === 3 && node.size === 0xc000n && node.argument === 0n) {
            const key = node.entry.toString();
            (groups[key] ||= []).push(node);
        }
    }
    const matches = Object.values(groups).filter(rows => rows.length === 7);
    check(matches.length === 1,
        "API-worker group is not unique (" + matches.length + " groups)");
    const workers = [];
    for (const node of matches[0]) {
        const current = await readFs(context, node.address, 0x1c0);
        check(current[0x40] === 3 && readU32LE(current, 0x1b0) === node.handle &&
            readU64LE(current, 0x68) === node.entry,
            "API thread " + hex(node.address) + " changed");
        const copy = await exportHandle(context, node.handle,
            "FS API thread " + node.id);
        const out = allocPage(0x40);
        runtime.write64(out, 0n);
        const identity = stubCallU64(NATIVE.threadId, out, copy.handle);
        check(identity.rc === 0 && identity.value === node.id,
            "copied API thread " + node.id + " identity check failed (rc=" +
            hex(identity.rc) + " value=" + identity.value + ")");
        workers.push({address: node.address, sourceHandle: node.handle,
            id: node.id, entry: node.entry, handle: copy.handle});
    }
    const cancellations = workers.map(worker => cancelThread(worker.handle));
    check(cancellations.every(rc => rc === 0),
        "API worker cancellation was rejected: " +
        cancellations.map(hex).join(","));
    context.apiWorkers = workers;
    await khLog("api workers bound: " + workers.map(w => hex(w.id)).join(" "));
    return workers;
}

// -- Gated workers ----------------------------------------------------------------

// Prepare a gated worker: a raw thread whose frame starts with a wait on a
// fresh SDK gate event followed by one send per request packet.
//
// requests: [{pkt, handle, label}] — each becomes one IPC send step.
// Returns the prepared worker descriptor (also stored as context.worker).
export function prepareGatedWorker(context, requests, label) {
    const gate = makeEvent(context, label + "-gate");
    const calls = [{fn: sdk_base + BigInt(EVENT_CODE.wait),
        args: [gate, 0n, 0n, 0n]}];
    const sendIndices = [];
    for (const request of requests) {
        sendIndices.push(calls.length);
        calls.push({fn: runtime.IPC_SEND_DISPATCHER,
            args: [BigInt(request.handle), request.pkt, 0x1000n, 0n]});
    }
    const frame = buildRawFrame(calls);
    const scheduling = borrowThreadScheduling();
    check(scheduling && scheduling.priority > 0,
        "no borrowed thread scheduling for " + label);
    const worker = createRawWorker(frame.frame, scheduling);
    check(worker && worker.rc === 0 && worker.handle,
        "raw worker creation failed for " + label + " rc=" + hex(worker && worker.rc));
    startRawWorker(worker);
    check(worker.started, label + " thread failed to start");
    // The worker must still be parked on the gate event.
    check(readFrameResult(frame.frame, 0) === runtime.FRAME_SENTINEL,
        label + " passed its start gate before release");
    const prepared = {label, gate, frame, worker, sendIndices, requests,
        released: false, joined: false};
    context.worker = prepared;
    return prepared;
}

export function releaseGatedWorker(prepared) {
    check(!prepared.released, "worker gate was already released");
    signalEvent(prepared.gate);
    prepared.released = true;
}

// Poll: results of each send step (sentinel while pending).
export function workerSendResults(prepared) {
    return prepared.sendIndices.map(index =>
        readFrameResult(prepared.frame.frame, index));
}

export function workerFinished(prepared) {
    return workerSendResults(prepared).every(value =>
        value !== runtime.FRAME_SENTINEL);
}

export function joinGatedWorker(prepared, timeoutMs = 60000) {
    const until = Date.now() + timeoutMs;
    while (!workerFinished(prepared)) {
        check(Date.now() < until, prepared.label +
            " did not finish; retaining its frame and handles");
        runtime.waitMs(10);
    }
    const waitRc = waitRawThread(prepared.worker.handle, 1000000000n);
    check(waitRc === 0, prepared.label + " wait after completion failed: " + hex(waitRc));
    closeRawThread(prepared.worker.handle);
    prepared.joined = true;
    return workerSendResults(prepared);
}

export function cancelApiWorkers(context) {
    const results = context.apiWorkers.map(worker => cancelThread(worker.handle));
    check(results.every(rc => rc === 0), "api worker cancellation failed");
    return results;
}
