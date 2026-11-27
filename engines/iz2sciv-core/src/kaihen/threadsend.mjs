// kaiHEN port — thread-context send probe (TS). Every send in this port so
// far — including every DX experiment — issued svc #0.22 from the loader's
// JS thread. The author engine's sends all execute on its native RPC thread
// (same process). If the FS server or the kernel keys anything off the
// sending thread / its TLS context, every state diff we ran is blind to it.
// This probe builds the OpenCodeFileSystem packet for the copied Loader root
// and executes the bare-send wrapper (sdk+0x168C54, byte-verified in wire.mjs)
// on a raw kernel thread through the wave frame machinery:
//   transport 0      -> thread context is the difference; parse the reply
//   transport 0xF601 -> the session is closed for every thread: server-side
// Runs right after copyLoaderRoot + the state dump, before the DX block.

import runtime from "../core/runtime.mjs";
import {check, khLog, hex, u64Bytes, arenaSlots, arenaAlloc, track}
    from "./state.mjs";
import {buildCmif} from "./wire.mjs";

const BARE_SEND_OFFSET = 0x168C54;

export async function threadSendProbe(context, title) {
    const copy = context.loaderCopy;
    check(copy && copy.handle, "thread-send requires the copied root");
    const slots = arenaSlots(context);

    // The same OpenCodeFileSystem request the donor stage sends: cmd 0,
    // attributes 1, storage 3, u64 title, receive buffer for the code info.
    const info = arenaAlloc(context, 0x2000, "ts-info");
    buildCmif(slots.pkt, {cmd: 0,
        input: [1, 3, 0, 0, 0, 0, 0, 0].concat(u64Bytes(BigInt(title))),
        b: [{addr: info, size: 0x1a4}]});

    const scheduling = runtime.borrowThreadScheduling();
    check(scheduling, "thread scheduling borrow failed");
    await khLog("TS0 frame ready pkt=" + hex(slots.pkt) +
        " handle=" + hex(copy.handle) + " prio=" + scheduling.priority +
        " core=" + scheduling.core);

    const frame = runtime.buildRawFrame([{
        fn: sdk_base + BigInt(BARE_SEND_OFFSET),
        args: [BigInt(slots.pkt), 0x1000n, BigInt(copy.handle), 0n]
    }]);
    const worker = runtime.launchRawWorker(BigInt(frame.frame), scheduling);
    check(worker.rc === 0 && worker.handle,
        "raw worker create failed: " + hex(worker.rc));
    track(context, worker.handle, "ts-worker");
    const waitRc = runtime.waitRawThread(worker.handle, 5000000000n);
    const result = runtime.readFrameResult(frame.frame, 0);
    await khLog("TS1 wait=" + hex(waitRc) + " raw=" +
        hex(Number(BigInt(result) & 0xffffffffn)) +
        " sentinel=" + (BigInt(result) === runtime.FRAME_SENTINEL));

    if (BigInt(result) !== runtime.FRAME_SENTINEL) {
        const transport = Number(BigInt(result) & 0xffffffffn) >>> 0;
        await khLog("TS2 THREAD-SEND transport=" + hex(transport));
        if (transport === 0) {
            let dump = "";
            for (let i = 0; i < 12; i++)
                dump += (dump ? " " : "") + hex(Number(runtime.read32(
                    slots.pkt + BigInt(i * 4))));
            await khLog("TS3 ***THREAD-SEND LIVED*** reply words: " + dump);
        }
    }
    try { runtime.closeHandle(worker.handle); } catch (error) { /* exited */ }
    return {ok: true};
}
