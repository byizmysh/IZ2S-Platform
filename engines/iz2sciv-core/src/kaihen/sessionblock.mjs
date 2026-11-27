// kaiHEN port — session-block probe (SB). SR localized the kill to
// 10-30ms after the copy (ref 2->1 lands there; SessionClosed observed at
// the first send 27ms after creation with NO dispatch in between — the
// send merely observes the corpse). SB finds the copied session's OWN
// server-side registration (a new {object, metadata, id, holderVptr} row
// in the manager pool, diffed pre/post copy) and watches its fields
// SEND-LESS for ~1s, then sends once afterwards:
//   - fields flip during the send-less trail  => creation-time async
//     validation (a sweep), first flip names the mechanism
//   - nothing flips, session alive at +1s, dies at the send => dispatch-
//     triggered after all (restores the run46 read)

import runtime from "../core/runtime.mjs";
import {check, khLog, hex, readU64LE, readU32LE, arenaSlots, arenaAlloc,
    track, u64Bytes} from "./state.mjs";
import {readFs, writeFs} from "./endpoint.mjs";
import {cmifSend, bareSendTransport, buildCmif} from "./wire.mjs";

const WIN_BACK = 0x800, WIN_LEN = 0x4800;   // manager pool window
const FIELD_BACK = 0x80, FIELD_FWD = 0x40;  // watch around a hit
const TRAIL_ROUNDS = 30, TRAIL_GAP_MS = 8;

export async function sessionBlockProbe(context, title) {
    const owned = context.inspectionDomain;
    const target = context.loaderRoot;
    const ep = context.ep;
    const layout = context.layout;
    check(owned && target && context.loaderCopy && ep && ep.armed && layout,
        "session-block requires the loader-copy state");

    const object = BigInt(target.object);
    const pairBytes = await readFs(context, target.entry + 0x20, 0x10);
    const metadata = readU64LE(pairBytes, 8);
    const holderVptr = BigInt(layout.holderVptr);
    const refField = target.object + 0x38;

    // Alive-row anchor: the inspection domain's SESSION object (DL3's
    // base.domain — vptr rootVptr+0x28e0), not our entry object. Its
    // registration row is what stays alive through the whole chain.
    const domainPtr = BigInt(context.state.domainLayout.domain);

    const winStart = layout.manager - WIN_BACK;
    const scan = async (label, obj) => {
        const bytes = await readFs(context, winStart, WIN_LEN);
        const hits = [];
        for (let k = FIELD_BACK; k + 0x20 <= WIN_LEN; k += 8) {
            if (readU64LE(bytes, k) !== obj) continue;
            hits.push({at: k,
                meta: readU64LE(bytes, k + 8),
                id: readU64LE(bytes, k + 0x10),
                holder: readU64LE(bytes, k + 0x18)});
        }
        await khLog("SB " + label + " hits=" + hits.length + " " +
            hits.slice(0, 6).map(h => hex(h.at) + ":id" +
                hex(Number(h.id & 0xffffn)) + "/" +
                (h.holder === holderVptr ? "Y" : "N")).join(" "));
        return hits;
    };

    const pre = await scan("0-pre", object);
    const preDom = await scan("0-pre-dom", domainPtr);
    const entry20 = context.loaderCopy.ownEntry + 0x20;
    const oldPair = await readFs(context, entry20, 0x10);
    const borrowPair = await readFs(context, target.entry + 0x20, 0x10);
    await writeFs(context, entry20, borrowPair);
    let copyHandle = 0;
    try {
        const slots = arenaSlots(context);
        const reply = cmifSend(owned.handle, {pkt: slots.pkt,
            control: true, cmd: 1,
            input: [owned.root & 0xff, owned.root >> 8 & 0xff,
                owned.root >> 16 & 0xff, owned.root >> 24 & 0xff],
            rawWords: 9});
        if (reply.transport === 0 && reply.moves && reply.moves.length)
            copyHandle = track(context, reply.moves[0], "sb-copy");
        await khLog("SB1 copy transport=" + hex(reply.transport) +
            " handle=" + hex(copyHandle));
    } finally {
        await writeFs(context, entry20, oldPair);
    }
    check(copyHandle, "SB copy failed — no handle to watch");

    const post = await scan("2-post", object);
    const preSet = new Set(pre.map(h => h.at));
    const fresh = post.filter(h => !preSet.has(h.at) && h.holder === holderVptr);
    await khLog("SB2 new rows=" + fresh.length + " " +
        fresh.map(h => hex(winStart + h.at)).join(" "));
    check(fresh.length >= 1 && fresh.length <= 4,
        "expected 1-4 new registration rows, got " + fresh.length);
    const domainRows = preDom.filter(h => h.holder === holderVptr);
    await khLog("SB2 domain rows=" + domainRows.length + " " +
        domainRows.map(h => hex(winStart + h.at)).join(" "));

    // Send-less field trail: the new rows (N), the alive domain rows (D),
    // and the copied object's ref (N:ref).
    const fieldsOf = (h, tag) => [
        ...(tag === "N" ? [{name: "N:ref", addr: refField}] : []),
        ...Array.from({length: (FIELD_BACK + FIELD_FWD) / 8}, (_, i) => ({
            name: tag + ":q" + (i - FIELD_BACK / 8),       // q-16 .. q+7
            addr: winStart + h.at - FIELD_BACK + i * 8}))
    ];
    const watches = [
        ...fresh.slice(0, 1).flatMap(h => fieldsOf(h, "N")),
        ...domainRows.slice(0, 1).flatMap(h => fieldsOf(h, "D"))];
    check(watches.length <= 60, "too many watch fields: " + watches.length);
    const readAll = async () => {
        const out = [];
        for (const w of watches)
            out.push(readU64LE(await readFs(context, w.addr, 8), 0));
        return out;
    };
    let last = await readAll();
    const trail = ["t0:N:ref=" + hex(Number(last[0] & 0xffffffffffn)) +
        (watches.some(w => w.name === "D:q-16") ?
            " D-block@" + hex(watches.find(w => w.name === "D:q-16").addr) : "")];
    let flips = 0;
    for (let t = 1; t <= TRAIL_ROUNDS && flips < 24; t++) {
        await runtime.waitMs(TRAIL_GAP_MS);
        const now = await readAll();
        for (let i = 0; i < now.length; i++) {
            if (now[i] !== last[i]) {
                trail.push("t" + t + ":" + watches[i].name + " " +
                    hex(Number(last[i] & 0xffffffffffn)) + ">" +
                    hex(Number(now[i] & 0xffffffffffn)));
                flips++;
            }
        }
        last = now;
    }
    await khLog("SB3 send-less trail [" + trail.join(" ") + "]");

    // Now the send: alive (transport 0) or already dead (0xF601)?
    const slots2 = arenaSlots(context);
    const info = arenaAlloc(context, 0x2000, "sb-info");
    buildCmif(slots2.pkt, {cmd: 0,
        input: [1, 3, 0, 0, 0, 0, 0, 0].concat(u64Bytes(BigInt(title))),
        b: [{addr: info, size: 0x1a4}]});
    const rc = bareSendTransport(copyHandle, slots2.pkt);
    const refEnd = readU64LE(await readFs(context, refField, 8), 0);
    await khLog("SB4 post-trail send rc=" + hex(rc) + " ref=" +
        hex(Number(refEnd & 0xffffffffn)));
    if (copyHandle) try { runtime.closeHandle(copyHandle); }
    catch (error) { /* dead */ }
    return {ok: true};
}
