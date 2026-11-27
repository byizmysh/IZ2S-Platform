// kaiHEN port — the save-filesystem donor: /main (the RO image) and
// /main.npdm (the real RO title's policy) at the root of the game's own save
// data, then the domain conversion that lets the launch swap redirect the
// Loader's mounted CodeFS to it.
//
// The pair is LEFT at the save root after the run: the next kaiHEN stage
// reuses it by hash (prepareDonor's reuse branch), so a subsequent boot
// neither rewrites nor deletes the game's save. The every-boot core cleanup
// no longer wipes it either (core/steps/cleanup.mjs). Only a changed
// payload/policy replaces it, through the stale-removal path below.

import runtime from "../core/runtime.mjs";
import {check, khLog, hex, u64Bytes, readU64LE, readNative,
    writeNative, track, arenaSlots} from "./state.mjs";
import {cmifSend, requireOk, oneMovedHandle, canonical} from "./wire.mjs";

const {openMount, closeHandle, writeStr} = runtime;

function savePathPacket(context, handle, cmd, name, extra) {
    const slots = arenaSlots(context);
    writeStr(slots.path, "/" + name);
    return cmifSend(handle, {
        pkt: slots.pkt, cmd,
        x: [{addr: slots.path, size: 0x301}],
        input: extra || []
    });
}

// Preflight: the donor root must contain nothing but our files.
const CHECK_NAMES = ["main", "main.npdm", "rtld", "sdk",
    ...Array.from({length: 10}, (_, i) => "subsdk" + i)];

export async function prepareDonor(context, files) {
    // files: {main: bytes, "main.npdm": bytes}
    const mount = openMount("save");
    check(mount && mount.handle, "could not clone the game's mounted save");
    const handle = mount.handle;
    track(context, handle, "donor-save");
    const donor = {handle, mount: "save", files: [], checkedAbsent: []};

    // Preflight every name.
    for (const name of CHECK_NAMES) {
        const reply = savePathPacket(context, handle, 7, name);
        check(reply.transport === 0,
            "donor preflight '" + name + "' transport " + hex(reply.transport));
        if (reply.result === 0) {
            check(["main", "main.npdm"].includes(name),
                "donor root unexpectedly contains /" + name);
        } else {
            check(canonical(reply.result) === 0x202,
                "donor preflight '" + name + "' failed: " + hex(reply.result));
            donor.checkedAbsent.push(name);
        }
    }

    // Create and write both files with full readback verification. A donor
    // file retained from an earlier run is REUSED when its content still
    // matches this run's payload/policy (open, size, byte compare) and
    // removed + committed away when stale — CreateFile on an existing path
    // is 0x402. Reuse is the normal path now: cleanupDonor keeps the pair.
    for (const name of ["main.npdm", "main"]) {
        const bytes = files[name];
        check(bytes && bytes.length > 0 && bytes.length <= 0x100000,
            "donor file " + name + " has no content");

        let stale = false;
        const exists = savePathPacket(context, handle, 7, name);
        check(exists.transport === 0,
            "donor existence '" + name + "' transport " + hex(exists.transport));
        if (exists.result === 0) {
            const opened = savePathPacket(context, handle, 8, name, [1, 0, 0, 0]);
            const child = oneMovedHandle(opened, "donor reuse open /" + name);
            try {
                const slots = arenaSlots(context);
                const sizeReply = cmifSend(child, {pkt: slots.pkt2, cmd: 4,
                    input: [], rawWords: 8});
                requireOk(sizeReply, "donor reuse size /" + name);
                const size = readU64LE(
                    readNative(slots.pkt2 + BigInt(sizeReply.dataAt), 8), 0);
                // A retained donor from an older payload or kaiHEN build can
                // have a different length. The pair is ours alone, so it goes
                // through the stale path (remove + re-create) instead of
                // failing the whole stage, which is what a wiped root used to
                // guarantee.
                if (size === BigInt(bytes.length) &&
                        await saveFileMatches(context, child, bytes)) {
                    donor.files.push({name, size: bytes.length, bytes,
                        verified: true, reused: true});
                    await khLog("donor /" + name + " reused");
                } else {
                    stale = true;
                }
            } finally {
                closeHandle(child);
            }
            if (stale) {
                const removed = savePathPacket(context, handle, 1, name);
                requireOk(removed, "stale donor removal /" + name);
                const commit = cmifSend(handle,
                    {pkt: arenaSlots(context).pkt, cmd: 10, input: [], rawWords: 8});
                requireOk(commit, "stale donor removal commit");
                const gone = savePathPacket(context, handle, 7, name);
                check(canonical(gone.result) === 0x202,
                    "stale donor /" + name + " still exists: " + hex(gone.result));
                await khLog("donor /" + name + " removed (stale)");
            }
        }
        if (donor.files.some(f => f.name === name)) continue;

        const created = savePathPacket(context, handle, 0, name,
            [0, 0, 0, 0, 0, 0, 0, 0].concat(u64Bytes(BigInt(bytes.length))));
        requireOk(created, "donor create /" + name);
        const entry = {name, size: bytes.length, bytes, created: true};
        donor.files.push(entry);

        const opened = savePathPacket(context, handle, 8, name, [3, 0, 0, 0]);
        const child = oneMovedHandle(opened, "donor open /" + name);
        try {
            for (let at = 0; at < bytes.length; at += 0x10000) {
                const part = bytes.slice(at, at + 0x10000);
                await writeSaveChunk(context, child, at, part);
            }
            check(await saveFileMatches(context, child, bytes),
                "donor /" + name + " readback differs");
            entry.verified = true;
        } finally {
            closeHandle(child);
        }
    }

    const committed = cmifSend(handle, {pkt: arenaSlots(context).pkt,
        cmd: 10, input: [], rawWords: 8});
    requireOk(committed, "donor commit");

    const converted = cmifSend(handle, {pkt: arenaSlots(context).pkt,
        control: true, cmd: 0, input: [], rawWords: 8});
    requireOk(converted, "donor ConvertToDomain");
    const slots = arenaSlots(context);
    const root = Number(runtime.read32(slots.pkt + BigInt(converted.dataAt))) >>> 0;
    check(root > 0 && root <= 0x4000, "donor domain id out of range: " + root);
    donor.root = root;
    donor.ready = true;
    context.donor = donor;
    await khLog("donor ready root=" + hex(root) + " files=" +
        donor.files.map(f => "/" + f.name + ":" + f.size).join(" "));
    return donor;
}

async function writeSaveChunk(context, handle, offset, bytes) {
    const slots = arenaSlots(context);
    writeNative(slots.data, bytes);
    const reply = cmifSend(handle, {
        pkt: slots.pkt, cmd: 1,
        input: [1, 0, 0, 0, 0, 0, 0, 0]
            .concat(u64Bytes(BigInt(offset)))
            .concat(u64Bytes(BigInt(bytes.length))),
        a: [{addr: slots.data, size: bytes.length}]
    });
    requireOk(reply, "donor write");
}

async function readSaveChunk(context, handle, offset, size) {
    const slots = arenaSlots(context);
    const reply = cmifSend(handle, {
        pkt: slots.pkt, cmd: 0,
        input: u64Bytes(0n).concat(u64Bytes(BigInt(offset)))
            .concat(u64Bytes(BigInt(size))),
        b: [{addr: slots.data, size}]
    });
    requireOk(reply, "donor readback");
    return readNative(slots.data, size);
}

// Read a save file back and compare it to the expected bytes chunk by chunk.
// No digest and no accumulated copy: a mismatch stops at the first differing
// chunk, and the comparison reads exactly the same bytes the hash would.
async function saveFileMatches(context, handle, expected) {
    for (let at = 0; at < expected.length; at += 0x10000) {
        const size = Math.min(0x10000, expected.length - at);
        const chunk = await readSaveChunk(context, handle, at, size);
        for (let i = 0; i < size; i++)
            if (chunk[i] !== expected[at + i]) return false;
    }
    return true;
}

// The post-launch donor step: verify the pair and settle the stage. The
// files are deliberately LEFT at the root of the game's save so the next
// kaiHEN run reuses them by content (prepareDonor), which skips the create +
// write + delete commits on the save while the FS endpoint is armed.
// Set REMOVE_DONOR_FILES to true to restore the old post-launch removal.
const REMOVE_DONOR_FILES = false;

// Verify the donor files and keep them (after the launch). Verification
// goes through a FRESH clone of the mounted save (reference donor-cleanup):
// the donor session itself stays converted to its domain.
export async function cleanupDonor(context) {
    const donor = context.donor;
    check(donor && donor.ready, "donor settle requires a ready donor");
    if (donor.cleanedUp) return {cleanedUp: true, retained: true};
    check(!context.worker || context.worker.joined,
        "donor settle requires the completed launch and an idle worker");
    const mount = runtime.openMount(donor.mount || "save");
    check(mount && mount.handle,
        "could not clone the mounted save to verify the donor");
    const handle = mount.handle;
    const kept = [];
    try {
        for (const item of donor.files) {
            if (item.removed) continue;
            check(item.verified, "donor file /" + item.name + " was never verified");
            const opened = savePathPacket(context, handle, 8, item.name, [1, 0, 0, 0]);
            const child = oneMovedHandle(opened, "donor verify /" + item.name);
            try {
                const slots = arenaSlots(context);
                const sizeReply = cmifSend(child, {pkt: slots.pkt2, cmd: 4,
                    input: [], rawWords: 8});
                requireOk(sizeReply, "donor size /" + item.name);
                const size = readU64LE(
                    readNative(slots.pkt2 + BigInt(sizeReply.dataAt), 8), 0);
                check(size === BigInt(item.size),
                    "donor /" + item.name + " changed");
                check(await saveFileMatches(context, child, item.bytes),
                    "donor /" + item.name + " content changed");
            } finally { closeHandle(child); }
            kept.push(item.name);
            if (!REMOVE_DONOR_FILES) continue;
            const removed = savePathPacket(context, handle, 1, item.name);
            requireOk(removed, "donor remove /" + item.name);
            const committed = cmifSend(handle, {pkt: arenaSlots(context).pkt,
                cmd: 10, input: [], rawWords: 8});
            requireOk(committed, "donor cleanup commit");
            const gone = savePathPacket(context, handle, 7, item.name);
            check(canonical(gone.result) === 0x202,
                "donor /" + item.name + " still exists");
            item.removed = true;
        }
    } finally {
        closeHandle(handle);
    }
    donor.cleanedUp = true;
    donor.retained = REMOVE_DONOR_FILES ? [] : kept;
    await khLog(REMOVE_DONOR_FILES
        ? "donor files removed"
        : "donor files kept: " + kept.map(n => "/" + n).join(" "));
    return REMOVE_DONOR_FILES
        ? {cleanedUp: true, files: kept}
        : {cleanedUp: true, retained: true, files: kept};
}
