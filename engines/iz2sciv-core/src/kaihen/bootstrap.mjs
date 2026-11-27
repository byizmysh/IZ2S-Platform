// kaiHEN port — the bootstrap NRO: 432-byte argument block (start-hen.cjs
// layout), the image build (bootstrap.bin + Civ7 trywait adapter), launch on
// a raw thread through the RO frontend, and the startup-record readback.

import runtime from "../core/runtime.mjs";
import {check, khLog, hex, readU64LE, readU32LE, readNative,
    writeNative, writeU64LE, arenaAlloc, arenaSlots, u64Bytes,
    HEN_TITLE, RO_TITLE}
    from "./state.mjs";
import {buildNroImage, tryWaitAdapterBytes, runNroOnRawThread} from "./ro.mjs";
import {bareSendTransport, cmifSend} from "./wire.mjs";
import {sendContext} from "./wire-context.mjs";
import {readFs, writeFs, armEndpoint} from "./endpoint.mjs";
import {copyLoaderRoot, exportHandle} from "./objects.mjs";
import {openCodeFileSystem} from "./codefs.mjs";
import {sdReadFile, openSdCard, sdGetEntryType, sdWriteFile}
    from "./sdcard.mjs";

// thread-code.json (game SDK).
export const PTHREAD = {
    create: 0x142180, join: 0x142480, tryWait: 0x137110,
    handleForm: "tagged", tagMask: 7
};

import {CORE_PACKAGE_PATH, SD_MANIFEST} from "../iz2s-runtime.mjs";

const ARG_BYTES = 432;

function hexToBytes(text) {
    const out = [];
    for (let i = 0; i + 1 < text.length; i += 2)
        out.push(parseInt(text.substr(i, 2), 16));
    return out;
}

export async function runBootstrap(context, options = {}) {
    context.__bsTag = Date.now();
    const ep0 = context.ep;
    await khLog("BS0 ep=" + (!!(context.ep && context.ep.armed)) +
        " tracked=" + (context.handles ? context.handles.length : "?"));
    // Reuse RUN25's bootstrap and IZ2S core, with the same 432-byte ABI as SB4.
    const sd = openSdCard(context);
    const stagedCore = sdGetEntryType(context, sd, SD_MANIFEST.core.path);
    check(stagedCore.transport === 0 && stagedCore.result === 0,
        SD_MANIFEST.core.path + " is missing on SD");

    // The bootstrap's first fs op (SdReader/SdWriter::begin) is cmd 18
    // (OpenSdCardFileSystem) on the fs_root session (arg 296). Our chain's
    // proven cmd-18 session is the promoted clone; the dedicated root's
    // pid-init may not be sufficient for it (op-17 failed 0x__720002 on
    // both boots). Probe both and hand the bootstrap the one that works.
    {
        const slots = arenaSlots(context);
        const try18 = (handle, label) => {
            const reply = cmifSend(handle,
                {pkt: slots.pkt, cmd: 18, input: [], rawWords: 8});
            const ok = reply.transport === 0 && reply.result === 0;
            if (reply.moves && reply.moves.length)
                try { runtime.closeHandle(reply.moves[0]); }
                catch (e) { /* already dead */ }
            khLog("BS-cmd18 " + label + " t=" + hex(reply.transport) +
                " result=" + hex(reply.result === undefined ?
                    0xffffffff : reply.result));
            return ok;
        };
        const plainOk = try18(context.fsRootPlain, "plain");
        const dedicatedOk = context.fsRootDedicated ?
            try18(context.fsRootDedicated, "dedicated") : false;
        context.bootstrapFsRoot = plainOk ? context.fsRootPlain :
            dedicatedOk ? context.fsRootDedicated : context.fsRootDedicated;
        await khLog("BS fs_root = " + (plainOk ? "plain" :
            dedicatedOk ? "dedicated" : "neither (fallback dedicated)"));
    }
    let text = sdReadFile(context, sd, SD_MANIFEST.bootstrap.path, 0x100000).bytes;
    check(text.length <= 0xff80,
        "bootstrap.bin exceeds the NRO text segment: " + text.length);

    // Append the Civ7 trywait adapter after the unmodified text.
    const adapterOffset = (text.length + 15) & ~15;
    const adapter = tryWaitAdapterBytes(Number(sdk_base) + PTHREAD.tryWait);
    const padded = text.concat(new Array(adapterOffset - text.length).fill(0));
    text = padded.concat(adapter);
    context.tryWaitAdapter = {offset: adapterOffset,
        target: Number(sdk_base) + PTHREAD.tryWait};

    const image = buildNroImage(text);
    check(image.length === 0x12000, "NRO image size differs");

    const args = await buildBootstrapArgs(context, options);
    const argsAddress = arenaAlloc(context, ARG_BYTES + 0x40, "bootstrap-args");
    context.bootstrapArgsAddress = Number(argsAddress);

    // Identity diagnostics: BS0 saw ep armed; the PP readFs saw context.ep
    // undefined (runs 119-125) — log whether the context object or its ep
    // changed identity across the SD read + image build.
    try {
        await khLog("PP-id tag=" + (context.__bsTag ? "same" : "CHANGED") +
            " ep0=" + (context.ep === ep0) +
            " ep=" + (!!(context.ep && context.ep.armed)) +
            " sd=" + (globalThis.__KH && globalThis.__KH.sd ?
                "cached" : "null") +
            " keys=" + Object.keys(context).length);
    } catch (logError) { /* survive log failure */ }
    if (!(context.ep && context.ep.armed)) {
        await khLog("PP re-arm required");
        await armEndpoint(context);
    }

    // The PP negotiation (SetCurrentProcess + copy-session probe/code-open)
    // moved to the loader-copy stage: fresh state, and the late-bootstrap
    // variant hit a heap-state flip where context.ep read armed and unarmed
    // with no intervening await (runs 119-127).

    // SessionClosed discipline (second opinion, runs 160+): 0xF601 is
    // kernel SessionClosed (module 1, description 123 — NOT Busy; Busy is
    // 0xF401, and 0xE401 = InvalidHandle confirmed the encoding). fs
    // closes a session when a request errors before its handler (sf_hipc
    // server manager R_CATCH_ALL -> CloseSessionImpl), so any malformed
    // send on the copy closes it for the NRO too — the SDK dispatcher's
    // 0x25A0B on those sends is the closed-session rejection. The author's
    // flow sends NOTHING on the copy between creation and the NRO's op-16,
    // and run139 (docked, pre-play-free) matched it. Our pre-play gate
    // (runs 142-159) closed the session ourselves. The original copy is
    // now never probed: read-only health checks only, and ONE disposable
    // minted copy absorbs the probe (the CC-open shape, result=0 on every
    // boot since run 74).
    {
        const lrow = context.bound.rows.find(
            r => r.domain === context.loaderRoot.domain);
        const rowState = lrow ? Number(readU32LE(
            await readFs(context, lrow.address + 0x40, 4), 0)) : -1;
        const donor = context.donorRow;
        const donorRefs = donor ? Number(readU32LE(
            await readFs(context, donor.object + 0x38, 4), 0)) : -1;
        const donorVptrOk = donor ? Number(readU64LE(
            await readFs(context, donor.object, 8), 0)) === donor.vptr
            : false;
        await khLog("BS-health row state=" + rowState +
            " donorRefs=" + donorRefs + " donorVptrOk=" + donorVptrOk +
            " original copy=" + hex(context.loaderCopy.handle));
        const original = context.loaderCopy;
        const savedCodefs = context.codefs;
        try {
            const disposable = await copyLoaderRoot(context, {mint: true});
            await khLog("BS-disposable copy " + hex(disposable));
            const probe = openCodeFileSystem(context, disposable, RO_TITLE);
            runtime.closeHandle(probe.handle);
            await khLog("BS-disposable probe OK (CC shape, result=0) — " +
                "session class alive; the NRO runs on the untouched " +
                "original");
        } catch (probeError) {
            await khLog("BS-disposable probe failed: " +
                (probeError && probeError.message ? probeError.message :
                    probeError) + " — running the NRO on the untouched " +
                "original regardless (its verdict is the truth)");
        } finally {
            context.loaderCopy = original;
            context.codefs = savedCodefs;
        }
    }

    const result = await runNroOnRawThread(context, image, argsAddress,
        args.bytes, {
            waitMs: options.waitMs || 90000,
            patchArgs: async (address, destination) => {
                // The ldr:ro session handle joins late; the adapter lives at
                // entry + adapterOffset inside the mapped image.
                args.u32(292, context.ro.handle);
                args.u64(272, Number(destination) + 0x80 +
                    context.tryWaitAdapter.offset);
                writeNative(address, args.bytes);
            }
        });
    // The bootstrap published its record at args+408 — read it BEFORE any
    // return-value check so a failure names the failing operation.
    const recordBytes = readNative(argsAddress + 408n, 24);
    const record = {
        operation: readU32LE(recordBytes, 0),
        result: readU32LE(recordBytes, 4),
        pid: readU64LE(recordBytes, 8).toString(),
        gateRestoreResult: readU32LE(recordBytes, 16),
        channelRestoreResult: readU32LE(recordBytes, 20)
    };
    // Author parity (start-hen.cjs:176): the bootstrap's readiness export
    // closes only after the NRO completes.
    if (context.bootstrapReadiness) {
        try { runtime.closeHandle(context.bootstrapReadiness.handle); }
        catch (closeError) { /* already gone */ }
        await khLog("bootstrap gate readiness closed " +
            hex(context.bootstrapReadiness.handle));
        context.bootstrapReadiness = null;
    }
    await khLog("IZ2S startup record: operation=" + record.operation +
        " result=0x" + record.result.toString(16) + " pid=" + record.pid +
        " returned=" + result.returned +
        " gateRestore=0x" + record.gateRestoreResult.toString(16) +
        " channelRestore=0x" + record.channelRestoreResult.toString(16));
    check(result.returned === 0,
        "bootstrap returned error " + result.returned);
    check(record.operation === 10,
        "startup operation " + record.operation + " did not reach Complete");
    check(record.result === 0,
        "startup operation " + record.operation + " failed: 0x" +
        record.result.toString(16));
    const gatePending = record.gateRestoreResult === 0xea01;
    check(record.gateRestoreResult === 0 || gatePending,
        "gate restore failed: " + hex(record.gateRestoreResult));
    check(record.channelRestoreResult === 0,
        "channel restore failed: " + hex(record.channelRestoreResult));
    record.gateRestorePending = gatePending;
    context.henStartup = record;
    await khLog("IZ2S startup record: operation=" + record.operation +
        " result=" + record.result + " pid=" + record.pid +
        " gateRestore=" + hex(record.gateRestoreResult) +
        (gatePending ? " (receipt transferred to IZ2S)" : ""));
    return record;
}

export async function buildBootstrapArgs(context, options = {}) {
    const launch = context.launch;
    const loaderDomain = context.loaderRoot.domain;
    const row = context.bound.rows.find(r => r.domain === loaderDomain);
    const donor = context.donorRow;
    const layout = context.layout;
    const event = context.gateEvent;
    // The bootstrap's gate needs its OWN readiness export: the author's
    // start-hen exports a fresh one ('Native launch Loader readiness',
    // start-hen.cjs:66) after launch.cjs closed the launch's, and closes
    // it only after the NRO completes (:176). Our launch now closes its
    // export at the author's point (run151's "bootstrap args need ..."
    // was this null), so make up a fresh one here.
    let exported = context.loaderServerExport;
    if (!exported) {
        exported = await exportHandle(context, row.receive,
            "bootstrap gate readiness");
        context.bootstrapReadiness = exported;
        await khLog("bootstrap gate readiness exported " + hex(exported.handle));
    }
    check(row && donor && event && exported && exported.handle,
        "bootstrap args need the launch session, donor, event, and export");
    check(context.apiWorkers && context.apiWorkers.length === 7,
        "bootstrap args need the seven API workers");

    const bytes = new Array(ARG_BYTES).fill(0);
    const u32 = (offset, value) => {
        bytes[offset] = value & 0xff; bytes[offset + 1] = value >> 8 & 0xff;
        bytes[offset + 2] = value >> 16 & 0xff;
        bytes[offset + 3] = value >>> 24;
    };
    const u64 = (offset, value) => writeU64LE(bytes, offset, BigInt(value));
    const rowVptr = readU64LE(hexToBytes(row.raw), 0);

    u32(0, context.candidateHandle);
    u64(8, context.state.endpoint.dataOffset);              // channel data
    u64(16, context.state.endpoint.dataEntryOffset + 0x20); // pointer field
    // gate: row address, vptr, domain, metadata, request, then 7 waiters.
    u64(24, row.address);
    u64(32, rowVptr);
    u64(40, row.domain);
    u64(48, row.metadata);
    u64(56, row.request);
    layout.waiters.forEach((address, index) => u64(64 + index * 8, address));
    context.apiWorkers.forEach((worker, index) => u32(120 + index * 4, worker.handle));
    [row.receive, row.wait, event.sourceHandle, exported.handle,
        event.read, event.write].forEach((value, index) => u32(148 + index * 4, value));
    // mount: table, loader domain, loader entry index, donor row, title.
    u64(176, layout.table);
    u64(184, loaderDomain);
    u64(192, (context.loaderRoot.entry - layout.table) / 0x30 + 1);
    u64(200, donor.entry);
    u64(208, donor.object);
    u64(216, donor.metadata);
    u64(224, donor.vptr);
    u64(232, HEN_TITLE);
    u32(240, context.pm.shell);
    u64(248, HEN_TITLE);
    u32(256, 0x14);
    bytes[260] = 3;
    u32(244, context.pm.monitor);
    u64(264, Number(sdk_base) + PTHREAD.create);
    u64(272, Number(sdk_base) + PTHREAD.tryWait); // replaced by the adapter
    u64(280, Number(sdk_base) + PTHREAD.join);
    u32(288, context.pm.sm);
    // fsRoot: the session that can actually open the SD (BS-cmd18 probe);
    // the bootstrap issues plain fsp-srv commands (cmd 10/18/51) on it.
    u32(296, context.bootstrapFsRoot || context.fsRootDedicated);
    const packagePath = CORE_PACKAGE_PATH;
    for (let i = 0; i < packagePath.length; i++)
        bytes[300 + i] = packagePath.charCodeAt(i);
    u32(396, context.fsProcess.handle);
    u32(400, context.notifier.handle);
    u32(404, context.loaderCopy.handle);

    return {bytes, u32, u64};
}
