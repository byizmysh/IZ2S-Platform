// kaiHEN port — the RO-frontend launch: gate the Loader's server session,
// fire a gated pm:shell LaunchTitle worker, watch the Loader's CodeFS mount
// arrive through the parked session, and swap the freshly mounted CodeFS
// adapter with the donor save filesystem so PM launches our image.

import runtime from "../core/runtime.mjs";
import {check, khLog, hex, u64Bytes, readU64LE, readU32LE, readNative,
    arenaAlloc, bytesEqual, RO_TITLE} from "./state.mjs";
import {readFs, writeFs, compareWriteFs, fsPointer, fsU64} from "./endpoint.mjs";
import {cmifSend, buildCmif, requireOk} from "./wire.mjs";
import {exportHandle} from "./objects.mjs";
import {Gate} from "./gate.mjs";
import {prepareGatedWorker, releaseGatedWorker, workerFinished,
    joinGatedWorker, workerSendResults} from "./workers.mjs";
import {pmIpcU64} from "./pm.mjs";

// Walk one domain's entry list in the table (reference launch.entries).
export async function domainEntries(context, domain) {
    const layout = context.layout;
    const sentinel = domain + 0x20;
    const rows = [];
    const seen = {};
    let at = await fsPointer(context, sentinel);
    while (at !== sentinel) {
        check(at >= layout.table && at < layout.table + layout.count * 0x30 &&
            (at - layout.table) % 0x30 === 0 && !seen[at] && rows.length < 128,
            "domain list left its verified entry table");
        seen[at] = true;
        const b = await readFs(context, at, 0x30);
        check(readU64LE(b, 0x18) === BigInt(domain),
            "domain entry owner changed");
        rows.push({id: (at - layout.table) / 0x30 + 1, entry: at, domain,
            object: Number(readU64LE(b, 0x20)),
            metadata: Number(readU64LE(b, 0x28))});
        at = Number(readU64LE(b, 0));
    }
    return rows;
}

// Parse a captured domain request (reference launch.requestInfo).
function requestInfo(bytes) {
    const first = readU32LE(bytes, 0), second = readU32LE(bytes, 4);
    let at = 8;
    if (second >>> 31) {
        const special = readU32LE(bytes, at);
        at += 4 + (special & 1 ? 8 : 0);
        at += 4 * ((special >>> 1 & 15) + (special >>> 5 & 15));
    }
    at += (first >>> 16 & 15) * 8;
    at += ((first >>> 20 & 15) + (first >>> 24 & 15) + (first >>> 28 & 15)) * 12;
    at = (at + 15) & ~15;
    check(at + 16 <= bytes.length, "saved request descriptors exceed the capture");
    const type = readU32LE(bytes, 0) & 0xffff;
    if (type === 5 || type === 7)
        return {kind: "control", command: readU32LE(bytes, at + 8)};
    if (type !== 4 && type !== 6) check(false, "unexpected Loader request type");
    const id = readU32LE(bytes, at + 4);
    if (bytes[at] === 2) return {kind: "close", object: id};
    check(bytes[at] === 1 && String.fromCharCode(bytes[at + 16], bytes[at + 17],
        bytes[at + 18], bytes[at + 19]) === "SFCI",
        "saved domain request is malformed");
    const end = at + 16 + (bytes[at + 2] | bytes[at + 3] << 8);
    check(end <= bytes.length && end >= at + 32,
        "saved domain request payload length is out of range");
    return {kind: "request", object: id, command: readU32LE(bytes, at + 24),
        input: bytes.slice(at + 32, end)};
}

export async function launchRoFrontend(context, options = {}) {
    const layout = context.layout;
    const loaderDomain = context.loaderRoot.domain;
    const rootId = (context.loaderRoot.entry - layout.table) / 0x30 + 1;
    const donor = context.donorRow;
    const title = options.title || RO_TITLE;
    const flags = 0x10;
    const wantedMount = 2;

    const server = await exportHandle(context,
        context.bound.rows.find(r => r.domain === loaderDomain).receive,
        "Loader server readiness");
    context.loaderServerExport = server;
    const gate = new Gate(context,
        context.bound.rows.find(r => r.domain === loaderDomain),
        server.handle, context.gateEvent);
    const proof = {target: title, flags, wantedMount, requests: [],
        mounts: 0, swapped: false};

    // Pre-build the pm:shell LaunchTitle packet and the gated worker.
    const pmPkt = arenaAlloc(context, 0x1000, "pm-launch");
    buildCmif(pmPkt, {cmd: 0,
        input: [flags & 0xff, flags >> 8 & 0xff, flags >> 16 & 0xff,
            flags >> 24 & 0xff, 0, 0, 0, 0]
            .concat(u64Bytes(BigInt(title)))
            .concat([3, 0, 0, 0, 0, 0, 0, 0]),
        rawWords: 16});
    const worker = prepareGatedWorker(context,
        [{pkt: pmPkt, handle: context.pm.shell, label: "pm-launch"}],
        "pm-launch");

    try {
        await gate.parkSession();
        proof.parked = true;
        releaseGatedWorker(worker);
        const deadline = Date.now() + 240000;
        for (let i = 0; i < 160; i++) {
            while (!(await gate.pending())) {
                if (workerFinished(worker)) break;
                check(Date.now() < deadline, "PM launch did not reach the parked FS session");
                runtime.waitMs(25);
            }
            if (workerFinished(worker)) break;
            const before = await domainEntries(context, loaderDomain);
            const step = await gate.step();
            const info = requestInfo(step.request);
            const after = await domainEntries(context, loaderDomain);
            const added = after.filter(a =>
                !before.some(b => b.id === a.id && b.object === a.object));
            const record = {index: i, ...info, added,
                removed: before.filter(b =>
                    !after.some(a => a.id === b.id && a.object === b.object))};
            proof.requests.push(record);
            await khLog("gate step " + i + ": " + info.kind +
                (info.command !== undefined ? " cmd=" + info.command : "") +
                " object=" + info.object + " added=" +
                added.map(a => a.id).join(","));
            if (info.kind === "request" && info.object === rootId && info.command === 0) {
                check(info.input.length >= 16 && info.input[0] === 1 &&
                    info.input[1] === 3 &&
                    readU64LE(info.input, 8) === BigInt(title),
                    "the queued CodeFS mount is not the selected native target");
                proof.mounts++;
                check(added.length === 1 && added[0].metadata === donor.metadata,
                    "target mount did not return one compatible filesystem");
                if (proof.mounts === wantedMount) {
                    const code = added[0];
                    const currentDonor = await readFs(context, donor.entry, 0x30);
                    check(bytesEqual(currentDonor.slice(0x18), donor.entryTail),
                        "donor domain entry changed before the exchange");
                    check((await fsPointer(context, code.object)) === donor.vptr &&
                        (await fsPointer(context, donor.object)) === donor.vptr,
                        "filesystem identity or concrete adapter type changed");
                    // Exchange the two domain entries' object pointers.
                    await compareWriteFs(context, code.entry + 0x20,
                        u64Bytes(BigInt(code.object)), u64Bytes(BigInt(donor.object)));
                    proof.firstWritten = true;
                    try {
                        await compareWriteFs(context, donor.entry + 0x20,
                            u64Bytes(BigInt(donor.object)),
                            u64Bytes(BigInt(code.object)));
                        proof.secondWritten = true;
                    } catch (error) {
                        check(await fsPointer(context, donor.entry + 0x20) ===
                            donor.object,
                            "donor write failed without restoration");
                        await compareWriteFs(context, code.entry + 0x20,
                            u64Bytes(BigInt(donor.object)),
                            u64Bytes(BigInt(code.object)));
                        proof.rolledBack = true;
                        throw error;
                    }
                    // fsPointer returns Number; loaderDomain/donor.domain are
                    // Numbers — BigInt() wrappers made === always false
                    // (run101: the exchange verified fine but this check
                    // could never pass).
                    check(await fsPointer(context, code.entry + 0x18) ===
                        loaderDomain &&
                        await fsPointer(context, code.entry + 0x20) ===
                        donor.object &&
                        await fsPointer(context, donor.entry + 0x18) ===
                        donor.domain &&
                        await fsPointer(context, donor.entry + 0x20) ===
                        code.object,
                        "owning-reference exchange did not verify");
                    proof.swapped = true;
                    proof.code = code;
                    await khLog("filesystem exchanged (mount " + proof.mounts + ")");
                    break;
                }
            }
        }
    } finally {
        proof.outstanding = gate.outstanding ? true : false;
        // Drain requests still QUEUED at the boundary while still parked.
        // Runs 147/148/149 photographed the alternative: the loader reads
        // the launched process's code THROUGH the swapped CodeFS (cmd 8
        // read chunks + the cmd-0 npdm open, payload 0x4F0 = 1264) in a
        // burst that starts at the exchange — a request from it queued at
        // the restore gets RECEIVED into the row buffer but never
        // dispatched, leaving the shared session permanently Busy. Run149
        // proved the drain works but an 8-step cap restores mid-burst and
        // request #9 wedges exactly the same. Drain until pending() is
        // genuinely quiet (the burst is ~10-30 chunks; 200 is headroom),
        // then restore.
        proof.drained = [];
        for (let drain = 0; drain < 200; drain++) {
            let queued = false;
            try { queued = await gate.pending(); }
            catch (pendingError) { break; }
            if (!queued) {
                if (drain > 0)
                    await khLog("launch drain quiet after " + drain);
                break;
            }
            try {
                const step = await gate.step();
                const info = requestInfo(step.request);
                proof.drained.push({index: drain, ...info});
                if (drain < 8 || (drain & 15) === 0)
                    await khLog("launch drain " + drain + ": " + info.kind +
                        (info.command !== undefined ? " cmd=" + info.command : "") +
                        " object=" + info.object);
            } catch (stepError) {
                await khLog("launch drain step failed: " +
                    (stepError && stepError.message ? stepError.message : stepError));
                break;
            }
        }
        await gate.restore();
        proof.restored = true;
    }

    const until = Date.now() + 20000;
    while (!workerFinished(worker)) {
        check(Date.now() < until, "PM did not complete after the gate was restored");
        runtime.waitMs(50);
    }
    const results = joinGatedWorker(worker);
    check(Number(results[0]) === 0,
        "pm-launch send failed: " + hex(Number(results[0])));

    // The author's launch closes the exported Loader-server-readiness
    // handle after the worker join (launch.cjs: root-native-close-handle
    // handle: server.handle) — before the result is even parsed. Our port
    // leaked it on every prior run (gate-test closes its own copy; this
    // export never was). A lingering export of the row's receive-side
    // signal object is the standing divergence from the reference in
    // exactly the machinery whose wake the post-restore requests need
    // (runs 148-150: the row receives the loader's npdm re-open on the
    // swapped CodeFS but fs never dispatches it).
    if (context.loaderServerExport) {
        try { runtime.closeHandle(context.loaderServerExport.handle); }
        catch (closeError) { /* already gone */ }
        await khLog("loader server readiness export closed " +
            hex(context.loaderServerExport.handle));
        context.loaderServerExport = null;
    }

    // The worker's send wrote the reply back into the packet page.
    const parsed = runtime.cmifReply(pmPkt);
    check(parsed.result === 0,
        "PM LaunchTitle failed: " + hex(parsed.result));
    const pidBytes = readNative(pmPkt + BigInt(parsed.data), 8);
    const pid = readU64LE(pidBytes, 0);
    check(pid > 1n && pid < 0x1000000n, "implausible launched pid " + pid);
    proof.result = 0;
    proof.pid = Number(pid);
    proof.imageAccepted = proof.swapped;
    context.launch = proof;
    context.launchGate = gate;
    await khLog("launch complete pid=" + pid + " swapped=" + proof.swapped);
    if (!proof.swapped) {
        // The unmodified process must not linger (reference cleanup).
        const terminated = pmIpcU64(context, context.pm.shell, 1, pid,
            "terminate unmodified launch");
        check(terminated.result === 0, "unmodified process cleanup failed");
        check(false, "launch did not swap the filesystem; process terminated");
    }
    return proof;
}
