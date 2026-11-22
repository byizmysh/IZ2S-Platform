// kaiHEN port — SD-card staging and reading through the promoted session
// (OpenSdCardFileSystem cmd 18 on the root object), plus the chunk-append API
// the PC launcher drives with embedded base64 payloads.

import runtime from "../core/runtime.mjs";
import {check, khLog, hex, readU64LE, readU32LE, readNative, writeNative,
    track, arenaSlots, bytesToHex, u64Bytes} from "./state.mjs";
import {cmifSend, requireOk} from "./wire.mjs";

const {closeHandle, allocPage, writeStr} = runtime;

const B64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

export function base64Decode(text) {
    const out = [];
    let bits = 0, acc = 0;
    for (let i = 0; i < text.length; i++) {
        const code = text.charCodeAt(i);
        if (code === 61) break; // '='
        const value = B64.indexOf(text[i]);
        if (value < 0) continue;
        acc = (acc << 6) | value;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push((acc >> bits) & 0xff);
        }
    }
    return out;
}

// -- SD filesystem ---------------------------------------------------------

// OpenSdCardFileSystem (cmd 18) through a PLAIN root clone: domain routing
// would deliver the filesystem as a domain object id, but every SD path
// command needs a plain session handle.
export function openSdCard(context) {
    if (globalThis.__KH.sd) return globalThis.__KH.sd;
    if (!context.fsRootPlain) {
        const plain = runtime.cloneObject(context.fspSrv.handle,
            context.fspSrv.rootObj);
        check(plain > 0, "plain root clone for the SD card failed");
        context.fsRootPlain = plain;
    }
    const slots = arenaSlots(context);
    const reply = cmifSend(context.fsRootPlain, {
        pkt: slots.pkt, cmd: 18, input: [], rawWords: 8
    });
    requireOk(reply, "OpenSdCardFileSystem");
    const handle = reply.moves.length ? reply.moves[0] : 0;
    check(handle, "SD filesystem open returned no handle");
    track(context, handle, "sdcard");
    globalThis.__KH.sd = handle;
    return handle;
}

function pathPacket(context, handle, cmd, path, extra) {
    const slots = arenaSlots(context);
    writeStr(slots.path, path);
    return cmifSend(handle, {
        pkt: slots.pkt, cmd,
        x: [{addr: slots.path, size: 0x301}],
        input: extra || []
    });
}

export function sdGetEntryType(context, sd, path) {
    return pathPacket(context, sd, 7, path);
}

export function sdCreateDirectory(context, sd, path) {
    const reply = pathPacket(context, sd, 2, path);
    requireOk(reply, "CreateDirectory " + path);
    return true;
}

export function ensureSdDirectories(context, sd, target) {
    const parts = target.split("/").slice(1, -1);
    let directory = "";
    for (const part of parts) {
        directory += "/" + part;
        const present = sdGetEntryType(context, sd, directory);
        if (present.transport === 0 && present.result !== 0)
            sdCreateDirectory(context, sd, directory);
    }
}

// Remove a file if it exists (idempotent restage).
export function removeSdFile(context, sd, path) {
    const present = sdGetEntryType(context, sd, path);
    if (present.transport === 0 && present.result === 0) {
        const removed = pathPacket(context, sd, 1, path);
        requireOk(removed, "remove stale " + path);
    }
}

// Create a file with the given size.
export function createSdFile(context, sd, path, size) {
    const created = pathPacket(context, sd, 0, path,
        [0, 0, 0, 0, 0, 0, 0, 0].concat(u64Bytes(BigInt(size))));
    requireOk(created, "create " + path);
}

export function sdOpenFile(context, sd, path, mode) {
    const reply = pathPacket(context, sd, 8, path,
        [mode & 0xff, 0, 0, 0]);
    check(reply.transport === 0 && reply.result === 0 && reply.moves.length === 1,
        "open " + path + " failed: transport=" + hex(reply.transport) +
        " result=" + hex(reply.result));
    return reply.moves[0];
}

export function sdFileSize(context, handle) {
    const slots = arenaSlots(context);
    const reply = cmifSend(handle, {pkt: slots.pkt2, cmd: 4, input: [], rawWords: 8});
    requireOk(reply, "GetSize");
    return readU64LE(readNative(slots.pkt2 + BigInt(reply.dataAt), 8), 0);
}

// IFile::Read (cmd 0) with a B descriptor into the arena data buffer.
// Input: {u64 pad, u64 offset, u64 size}.
export function sdReadFileChunk(context, handle, offset, size) {
    const slots = arenaSlots(context);
    const reply = cmifSend(handle, {
        pkt: slots.pkt, cmd: 0,
        input: u64Bytes(0n).concat(u64Bytes(BigInt(offset)))
            .concat(u64Bytes(BigInt(size))),
        b: [{addr: slots.data, size}]
    });
    requireOk(reply, "file read");
    const count = readU64LE(readNative(slots.pkt + BigInt(reply.dataAt), 8), 0);
    check(count === BigInt(size), "file read short: " + count + " of " + size);
    return readNative(slots.data, size);
}

// IFile::Write (cmd 1) with an A descriptor from the arena data buffer.
// Input: {u32 writeOption=1, u32 pad, u64 offset, u64 size}.
export function sdWriteFileChunk(context, handle, offset, bytes) {
    const slots = arenaSlots(context);
    writeNative(slots.data, bytes);
    const reply = cmifSend(handle, {
        pkt: slots.pkt, cmd: 1,
        input: [1, 0, 0, 0, 0, 0, 0, 0]
            .concat(u64Bytes(BigInt(offset)))
            .concat(u64Bytes(BigInt(bytes.length))),
        a: [{addr: slots.data, size: bytes.length}]
    });
    requireOk(reply, "file write");
    return true;
}

export function sdCommit(context, sd) {
    const slots = arenaSlots(context);
    const reply = cmifSend(sd, {pkt: slots.pkt, cmd: 10, input: [], rawWords: 8});
    requireOk(reply, "SD commit");
    return true;
}

// -- Full-file operations ------------------------------------------------------

// Read an entire SD file (bounded) for on-console use (bootstrap, donor).
export function sdReadFile(context, sd, path, maxLen) {
    const handle = sdOpenFile(context, sd, path, 1);
    try {
        const size = sdFileSize(context, handle);
        check(size > 0n && size <= BigInt(maxLen || 0x100000),
            path + " size " + size + " out of bounds");
        const parts = [];
        for (let at = 0n; at < size; at += 0x10000n)
            parts.push(sdReadFileChunk(context, handle, Number(at),
                Number(size - at > 0x10000n ? 0x10000n : size - at)));
        let out = [];
        for (const part of parts) out = out.concat(part);
        return {bytes: out, size: Number(size)};
    } finally {
        closeHandle(handle);
    }
}

// Create (or replace) a file on the SD card from a JS byte array.
// Only for bounded payloads; large files stream through the sink instead.
export function sdWriteFile(context, sd, path, bytes) {
    ensureSdDirectories(context, sd, path);
    removeSdFile(context, sd, path);
    const created = pathPacket(context, sd, 0, path,
        [0, 0, 0, 0, 0, 0, 0, 0].concat(u64Bytes(BigInt(bytes.length))));
    requireOk(created, "create " + path);
    const handle = sdOpenFile(context, sd, path, 3);
    try {
        for (let at = 0; at < bytes.length; at += 0x10000)
            sdWriteFileChunk(context, handle, at, bytes.slice(at, at + 0x10000));
    } finally {
        closeHandle(handle);
    }
    sdCommit(context, sd);
    return true;
}

// -- Chunked staging API (driven by generated payloads) --------------------------

// status: sizes of manifest paths that already exist on the SD card.
export function stagingStatus(context) {
    const sd = openSdCard(context);
    const out = {};
    for (const [name, item] of Object.entries(globalThis.__KH_MANIFEST || {})) {
        const entry = sdGetEntryType(context, sd, item.path);
        if (entry.transport === 0 && entry.result === 0) {
            const handle = sdOpenFile(context, sd, item.path, 1);
            try { out[name] = sdFileSize(context, handle).toString(); }
            finally { closeHandle(handle); }
        } else out[name] = null;
    }
    return out;
}

// Begin staging a file: create it and remember its handle.
export function stageBegin(context, name, path, size) {
    const sd = openSdCard(context);
    const staging = globalThis.__KH.staging ||= {};
    check(!staging[name], name + " is already being staged");
    ensureSdDirectories(context, sd, path);
    removeSdFile(context, sd, path);
    createSdFile(context, sd, path, size);
    const handle = sdOpenFile(context, sd, path, 3);
    staging[name] = {path, size, offset: 0, handle};
    return {ok: true, offset: 0};
}

// Append one base64 chunk to a staging file.
export function stageChunk(context, name, offset, base64) {
    const staging = globalThis.__KH.staging;
    const entry = staging && staging[name];
    check(entry, name + " is not being staged");
    check(offset === entry.offset, name + " chunk offset mismatch: " +
        offset + " != " + entry.offset);
    const bytes = base64Decode(base64);
    sdWriteFileChunk(context, entry.handle, offset, bytes);
    entry.offset += bytes.length;
    return {ok: true, offset: entry.offset, bytes: bytes.length};
}

// Finish staging: close, commit, verify size.
export function stageFinish(context, name) {
    const staging = globalThis.__KH.staging;
    const entry = staging && staging[name];
    check(entry, name + " is not being staged");
    check(entry.offset === entry.size, name + " staged " + entry.offset +
        " of " + entry.size + " bytes");
    closeHandle(entry.handle);
    sdCommit(context, openSdCard(context));
    delete staging[name];
    return {ok: true, size: entry.size};
}

// -- Release relocation -------------------------------------------------------
// Someone extracted the kaiHEN release zip straight onto the SD card root
// (/kaiHEN-20261114d/...). The expected layout is /kaihen/bin/*, /hbmenu.nro
// and /switch/ounceshell.nro — move the files into place with the
// IFileSystem rename commands (5 RenameFile, 6 RenameDirectory) instead of
// asking the user to shuffle folders by hand.

const RELEASE_SOURCES = {
    "bootstrap": {source: "bootstrap.bin", directory: false},
    "ro": {source: "ro", directory: true},
    "hen": {source: "hen", directory: true},
    "game": {source: "game", directory: true},
    "hbmenu": {source: "hbmenu.nro", directory: false},
    "programs": {source: "programs/ounceshell.nro", directory: false}
};

function sdRenamePath(context, sd, from, to, directory) {
    const slots = arenaSlots(context);
    writeStr(slots.path, from);
    writeStr(slots.info, to);
    const reply = cmifSend(sd, {pkt: slots.pkt,
        cmd: directory ? 6 : 5,
        x: [{addr: slots.path, size: 769}, {addr: slots.info, size: 769}],
        input: [], rawWords: 8});
    requireOk(reply, "rename " + from + " -> " + to);
    sdCommit(context, sd);
    return true;
}

// Move every manifest file in `missing` from a detected release folder at
// the SD root into its expected place. Returns the manifest names moved.
export async function relocateSdRelease(context, missing) {
    const manifest = globalThis.__KH_MANIFEST || {};
    const wanted = missing.filter(name => manifest[name] && RELEASE_SOURCES[name]);
    if (!wanted.length) return [];
    const sd = openSdCard(context);
    const list = runtime.ipcListDirectory(sd, "/", 3);
    check(list.error === 0, "SD root listing failed: " + list.error);
    // Candidate: a root entry named kaiHEN* (any case) that is not our own
    // lowercase /kaihen target and holds bootstrap.bin at its root.
    const candidates = list.entries.filter(entry =>
        entry.type === 0 && /^kaihen/i.test(entry.name) &&
        entry.name.toLowerCase() !== "kaihen");
    for (const candidate of candidates) {
        const root = "/" + candidate.name;
        const probe = sdGetEntryType(context, sd, root + "/bootstrap.bin");
        if (!(probe.transport === 0 && probe.result === 0)) continue;
        const moved = [];
        for (const name of wanted) {
            const item = RELEASE_SOURCES[name];
            const from = root + "/" + item.source;
            const to = manifest[name].path;
            const source = sdGetEntryType(context, sd, from);
            if (!(source.transport === 0 && source.result === 0)) continue;
            const target = sdGetEntryType(context, sd, to);
            if (target.transport === 0 && target.result === 0) continue;
            try {
                ensureSdDirectories(context, sd, to);
                sdRenamePath(context, sd, from, to, item.directory);
                moved.push(name);
            } catch (error) {
                await khLog("SD relocate " + name + " failed: " +
                    (error && error.message ? error.message : error));
            }
        }
        if (moved.length) {
            await khLog("SD: relocated " + moved.join(", ") +
                " from " + root + " (extracted release folder)");
            return moved;
        }
    }
    return [];
}

// Flag file: when /kaihen/civswitchfs-skipcheck exists, the chain skips
// file-identity checks (sizes vs the pinned manifest) so a different
// kaiHEN build can be run; presence is still required.
export function sdSkipCheckPresent(context) {
    const sd = openSdCard(context);
    const probe = sdGetEntryType(context, sd, "/kaihen/civswitchfs-skipcheck");
    return probe.transport === 0 && probe.result === 0;
}
