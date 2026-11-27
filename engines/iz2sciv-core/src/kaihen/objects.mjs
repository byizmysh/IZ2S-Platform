// kaiHEN port — owned FS objects: SystemDataUpdateEventNotifier, the handle
// export trick, the inspection domain, object location by domain ID, and the
// Loader-root session copy through a borrowed domain slot.

import runtime from "../core/runtime.mjs";
import {check, khLog, hex, u64Bytes, readU64LE, readU32LE, bytesEqual,
    readNative, writeNative, track, verifySdkCode} from "./state.mjs";
import {readFs, writeFs, compareWriteFs, fsPointer, fsU32, fsU64} from "./endpoint.mjs";
import {cmifSend, requireOk, oneMovedHandle, buildCmif, bareSendTransport}
    from "./wire.mjs";
import {arenaSlots} from "./state.mjs";
import {openService} from "./ro.mjs";

const {openDomainSession, closeHandle} = runtime;

// -- Notifier -----------------------------------------------------------------

// Open the SystemDataUpdateEventNotifier (cmd 510) through a plain clone of
// the promoted root (the reference uses a plain session clone; a domain-routed
// request would return a domain object id instead of a session handle), then
// locate its FS object by domain ID.
export async function openNotifier(context) {
    const slots = arenaSlots(context);
    // One plain root clone serves the notifier open and the bootstrap's
    // fsRoot argument (start-hen args+296).
    const fsRootPlain = runtime.cloneObject(context.fspSrv.handle,
        context.fspSrv.rootObj);
    check(fsRootPlain > 0, "plain root clone for the notifier failed");
    context.fsRootPlain = fsRootPlain;
    track(context, fsRootPlain, "fs-root-plain");

    const reply = cmifSend(fsRootPlain, {
        pkt: slots.pkt, cmd: 510, input: [], rawWords: 8
    });
    requireOk(reply, "OpenSystemDataUpdateEventNotifier");
    check(reply.moves.length === 1 && !reply.copies.length,
        "notifier did not return one moved handle");
    const handle = reply.moves[0];
    track(context, handle, "notifier");

    let proof = null;
    // The type/refcount checks must run while the locator clone is alive
    // (reference objects.cjs: refcount 2 = original + locator clone).
    const located = await locateByClone(context, handle, async location => {
        const before = await readFs(context, location.object, 0x18);
        check(readU64LE(before, 0) === BigInt(context.layout.rootVptr - 0x16c0) &&
            readU32LE(before, 0x10) === 2,
            "notifier object type or reference count differs");
        const B = Number(readU64LE(before, 8));
        check(B >= 0x100000000 && B < 0x8000000000,
            "notifier backend pointer invalid");
        const backend = await readFs(context, B, 0x48);
        check(readU64LE(backend, 0x10) === BigInt(B + 0x10) &&
            readU64LE(backend, 0x18) === BigInt(B + 0x10),
            "notifier event-list identity differs");
        const eventHandle = readU32LE(backend, 0x24);
        // The handle number grows with FS session churn. Retain the lower
        // sanity check; GetEventHandle verifies the actual handle below.
        check(eventHandle >= 0x8000,
            "notifier event handle implausible: " + hex(eventHandle));
        proof = {object: location.object, B, handleField: B + 0x24, eventHandle};
    });
    check(proof, "notifier inspection did not run");

    // Clone the event through GetEventHandle (cmd 0) once: the game keeps a
    // readable copy and the identity is verified.
    const eventReply = cmifSend(handle, {pkt: slots.pkt2, cmd: 0, input: [], rawWords: 8});
    requireOk(eventReply, "notifier GetEventHandle");
    check(eventReply.copies.length === 1 && !eventReply.moves.length,
        "notifier GetEventHandle did not copy one handle");
    const readEvent = eventReply.copies[0];
    track(context, readEvent, "notifier-event");

    // After the clone the field must still be the original event handle.
    const after = await readFs(context, proof.handleField, 8);
    check(readU32LE(after, 0) === proof.eventHandle,
        "notifier handle field changed");

    context.notifier = {handle, object: proof.object, B: proof.B,
        handleField: proof.handleField, eventHandle: proof.eventHandle,
        readEvent, located};
    await khLog("notifier object=" + hex(proof.object) + " B=" + hex(proof.B) +
        " event=" + hex(proof.eventHandle));

    // Dedicated pid-initialized fsp-srv for the bootstrap (the author's
    // root-open: a FRESH openService('fsp-srv') + Initialize(cmd 1, pid) —
    // never a clone of the promoted session, and the promoted clone must
    // NOT be re-initialized; doing so corrupted the shared session state
    // and the notifier open failed on the next two boots with 0xBC320002/
    // 0xEEB20002).
    const dedicated = openService("fsp-srv");
    check(dedicated > 0, "dedicated fsp-srv open failed");
    track(context, dedicated, "fsp-dedicated");
    const dSlots = arenaSlots(context);
    const rootInit = cmifSend(dedicated, {pkt: dSlots.pkt2, cmd: 1,
        input: u64Bytes(0n), pid: true, rawWords: 10});
    requireOk(rootInit, "dedicated root Initialize");
    context.fsRootDedicated = dedicated;
    await khLog("dedicated fsp-srv initialized handle=" + hex(dedicated));
    return context.notifier;
}

// Locate a game-owned FS session object by cloning it to a handle and
// converting that clone to a temporary domain (reference locator-open).
// `inspect(location)` runs while the locator clone is still alive.
export async function locateByClone(context, sourceHandle, inspect) {
    const slots = arenaSlots(context);
    const clone = oneMovedHandle(cmifSend(sourceHandle,
        {pkt: slots.pkt2, control: true, cmd: 2, input: [], rawWords: 8}),
        "clone for location");
    track(context, clone, "locator-clone");
    try {
        const converted = cmifSend(clone,
            {pkt: slots.pkt, control: true, cmd: 0, input: [], rawWords: 8});
        requireOk(converted, "locator ConvertToDomain");
        const root = Number(runtime.read32(slots.pkt + BigInt(converted.dataAt))) >>> 0;
        check(root > 0 && root <= 0x4000, "locator ID outside table: " + root);
        const entry = context.layout.table + (root - 1) * 0x30;
        const entryBytes = await readFs(context, entry, 0x30);
        const owner = Number(readU64LE(entryBytes, 0x18));
        const object = Number(readU64LE(entryBytes, 0x20));
        check(owner >= 0x100000000 && owner < 0x8000000000 && object > 0,
            "locator entry has no live owner or object");
        const domain = await readFs(context, owner, 0x20);
        check(readU64LE(domain, 0) === BigInt(context.layout.rootVptr + 0x28e0) &&
            readU64LE(domain, 8) === BigInt(context.layout.rootVptr + 0x2938) &&
            readU64LE(domain, 0x18) === BigInt(context.layout.manager),
            "locator domain does not use the verified manager");
        const location = {root, entry, owner, object, clone};
        if (inspect) await inspect(location);
        return location;
    } finally {
        const rc = Number(closeHandle(clone)) >>> 0;
        check(rc === 0, "locator clone close failed: " + hex(rc));
    }
}

// -- Handle export -------------------------------------------------------------

// Copy any FS-process handle into the game through the notifier: overwrite
// the notifier's stored event handle, call GetEventHandle, restore the field.
export async function exportHandle(context, handle, label) {
    check(Number.isInteger(handle) && handle > 0 &&
        (handle < 0x80000000 || handle === 0xffff8001),
        "exportHandle requires an existing FS handle");
    const notifier = context.notifier;
    const slots = arenaSlots(context);
    const before = await readFs(context, notifier.handleField, 4);
    check(readU32LE(before, 0) === notifier.eventHandle,
        "notifier handle field changed before export of " + label);
    const replacement = [handle & 0xff, handle >> 8 & 0xff,
        handle >> 16 & 0xff, handle >> 24 & 0xff];
    const entry = {label, sourceHandle: handle, started: Date.now(), restored: false};
    try {
        await compareWriteFs(context, notifier.handleField,
            before.slice(), replacement);
        const reply = cmifSend(notifier.handle,
            {pkt: slots.pkt2, cmd: 0, input: [], rawWords: 8});
        requireOk(reply, "export " + label);
        check(reply.copies.length === 1 && !reply.moves.length,
            "export " + label + " did not copy one handle");
        entry.handle = reply.copies[0];
        track(context, entry.handle, "export-" + label);
    } finally {
        await compareWriteFs(context, notifier.handleField,
            replacement, before.slice());
        entry.restored = true;
    }
    const restored = await readFs(context, notifier.handleField, 4);
    check(bytesEqual(restored, before), "notifier handle field did not restore");
    await khLog("exported " + label + " as " + hex(entry.handle));
    return entry;
}

// -- Inspection domain and the Loader-root copy ---------------------------------

// A second, plain fsp-srv domain session used for object copies.
export async function openInspectionDomain(context) {
    const opened = openDomainSession();
    check(opened && opened.handle && opened.rootObj,
        "inspection domain session failed to open");
    context.inspectionDomain = {handle: opened.handle, root: opened.rootObj};
    track(context, opened.handle, "inspection-domain");
    await khLog("inspection domain handle=" + hex(opened.handle) +
        " root=" + hex(opened.rootObj));
    return context.inspectionDomain;
}

// Borrow our inspection-domain slot once to copy the Loader's root session
// (reference copy-loader-root). Returns the copied root handle in the game.
export async function copyLoaderRoot(context, options = {}) {
    const mint = !!(options && options.mint);
    const layout = context.layout;
    const owned = context.inspectionDomain;
    const target = context.loaderRoot;
    const slots = arenaSlots(context);

    const ownEntry = layout.table + (owned.root - 1) * 0x30;
    const original = await readFs(context, ownEntry, 0x30);
    const ownDomain = Number(readU64LE(original, 0x18));
    const ownObject = Number(readU64LE(original, 0x20));
    check(ownDomain !== target.domain, "inspection slot shares the Loader domain");
    check(await fsPointer(context, ownDomain + 0x18) === layout.manager &&
        await fsU64(context, ownDomain) === await fsU64(context, target.domain),
        "owned domain identity differs from the Loader domain");

    const ownBefore = await readFs(context, ownObject, 0x40);
    const targetBefore = await readFs(context, target.object, 0x40);
    const callerPid = context.state.identityObjects.find(f => f.name === "root").pid;
    check(readU64LE(ownBefore, 0x30) === BigInt(callerPid) &&
        readU32LE(ownBefore, 0x38) === 1,
        "inspection slot root is not a fresh caller-owned fsp root");
    check(readU64LE(targetBefore, 0x30) === 1n && readU32LE(targetBefore, 0x38) === 1 &&
        readU64LE(targetBefore, 8) === readU64LE(ownBefore, 8) &&
        readU64LE(targetBefore, 0) - readU64LE(ownBefore, 0) === 0x1ea0n,
        "target differs from the native Loader-root layout");

    // Reference-count challenge on our own object through CopyFromCurrentDomain.
    const baseline = oneMovedHandle(cmifSend(owned.handle,
        {pkt: slots.pkt, control: true, cmd: 1,
            input: [owned.root & 0xff, owned.root >> 8 & 0xff,
                owned.root >> 16 & 0xff, owned.root >> 24 & 0xff], rawWords: 9}),
        "owned slot reference challenge");
    try {
        check(await fsU32(context, ownObject + 0x38) === 2,
            "owned slot failed its reference challenge");
    } finally {
        const rc = Number(closeHandle(baseline)) >>> 0;
        check(rc === 0, "challenge handle close failed");
    }
    check(await fsU32(context, ownObject + 0x38) === 1,
        "owned reference challenge did not restore");

    // Borrow the slot: point our domain entry at the Loader's object pair,
    // copy it out, and restore the slot immediately.
    const oldPair = await readFs(context, ownEntry + 0x20, 0x10);
    const borrowedPair = await readFs(context, target.entry + 0x20, 0x10);
    await writeFs(context, ownEntry + 0x20, borrowedPair);
    let copyHandle = 0;
    try {
        copyHandle = oneMovedHandle(cmifSend(owned.handle,
            {pkt: slots.pkt, control: true, cmd: 1,
                input: [owned.root & 0xff, owned.root >> 8 & 0xff,
                    owned.root >> 16 & 0xff, owned.root >> 24 & 0xff], rawWords: 9}),
            "Loader root copy");
        track(context, copyHandle, "loader-root-copy");
    } finally {
        await writeFs(context, ownEntry + 0x20, oldPair);
    }
    const restoredEntry = await readFs(context, ownEntry + 0x18, 0x18);
    check(bytesEqual(restoredEntry, original.slice(0x18)),
        "owned domain slot did not restore");
    if (mint) {
        // A minted (disposable) copy: the first copy already holds a
        // reference, so target refs are 3 here, not 2 — log instead of
        // assert, and do not touch context.loaderCopy.
        await khLog("mint refs own=" +
            Number(await fsU32(context, ownObject + 0x38)) +
            " target=" + Number(await fsU32(context, target.object + 0x38)));
    } else {
        check(await fsU32(context, ownObject + 0x38) === 1 &&
            await fsU32(context, target.object + 0x38) === 2,
            "unexpected reference counts after the Loader copy");
    }

    if (!mint) {
        context.loaderCopy = {handle: copyHandle, ownDomain, ownObject, ownEntry,
            target, ownBefore, targetBefore};
    }
    await khLog((mint ? "loader root minted as handle " :
        "loader root copied as handle ") + hex(copyHandle));
    return copyHandle;
}
