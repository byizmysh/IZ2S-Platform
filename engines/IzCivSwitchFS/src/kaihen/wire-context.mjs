// kaiHEN port — WithContext (type 6) domain-routed CMIF. The modern fs
// protocol: requests carry a context header {slot-token, domain-object-id}
// after the descriptors (aligned to 0x10), routing goes through the
// inspection domain session, and replies "add" new objects to the domain
// (their ids appear in the reply payload). Handles in reply payloads are
// the SERVER's table values — kernel-invalid in our process (0xE401
// InvalidHandle) — so all follow-up traffic must keep routing by object id.
//
// Layout decoded from the author engine's recorded working traffic
// (runs/switch2-5T5Brx .../launch-*.json steps[]):
//   [w0 type|counts][w1 words][X descs (2w)][B descs (3w, mode 0)]
//   [pad to 0x10][context {token, object, 0, 0}][SFCI + ver + cmd + 0][input]
// (steps[3]: 1 X desc -> context @0x10, SFCI @0x20;
//  steps[2]: 1 B desc -> context @0x20, SFCI @0x30.)

import runtime from "../core/runtime.mjs";
import {khLog, hex, writeNative} from "./state.mjs";
import {bareSendTransport} from "./wire.mjs";

const CTX_TOKEN = 0x00200001;

export function buildContextPacket(pkt, options) {
    const xList = options.x || [], bList = options.b || [];
    const input = options.input || [];
    for (let i = 0n; i < 0x200n; i += 8n) runtime.write64(pkt + i, 0n);

    runtime.write32(pkt, BigInt(
        (0x6 | (xList.length << 16) | (bList.length << 24)) >>> 0));
    const payloadWords = 16 + Math.ceil(input.length / 4) +
        (options.pid ? 3 : 0);
    // A pid descriptor rides in a special header: w1 bit 31, then the flag
    // word + 8-byte pid slot precede the descriptors (their recorded
    // SetCurrentProcess steps[1]: w1=0x8000000e, w2=1, pid slot, ctx @0x20).
    runtime.write32(pkt + 4n, BigInt((payloadWords |
        (options.pid ? 0x80000000 : 0)) >>> 0));

    let at = 8;
    if (options.pid) {
        runtime.write32(pkt + BigInt(at), 1n);          // pid flag
        at += 4 + 8;                                     // + pid slot
    }
    for (const d of xList) {                       // X: 2 words
        const address = BigInt(d.addr);
        const word0 = ((Number(d.size) << 16) |
            (Number((address >> 36n) & 7n) << 6) |
            (Number((address >> 32n) & 15n) << 12)) >>> 0;
        runtime.write32(pkt + BigInt(at), BigInt(word0));
        runtime.write32(pkt + BigInt(at + 4),
            BigInt(Number(address & 0xffffffffn)));
        at += 8;
    }
    for (const d of bList) {                       // B: 3 words; attr in bits 0-1
        const address = BigInt(d.addr);
        runtime.write32(pkt + BigInt(at), BigInt(Number(d.size)));
        runtime.write32(pkt + BigInt(at + 4),
            BigInt(Number(address & 0xffffffffn)));
        // Attribute bits 0-1 of the packed word: their read buffer used
        // attr 1 (0xc0000009), their code-info buffer attr 0 (0x90000004).
        // attr 0 and 3 both killed the domain on reads; 1 is the recorded
        // working value. Address bits: 32-35 -> bits 28-31, 36-38 -> bits 2-4.
        const attr = BigInt(d.attr || 0);
        const flags = attr | (((address >> 32n) & 0xfn) << 28n) |
            (((address >> 36n) & 7n) << 2n);
        runtime.write32(pkt + BigInt(at + 8), BigInt(Number(flags)));
        at += 12;
    }

    const ctxAt = (at + 0xf) & ~0xf;               // align to 0x10
    // Per-command TLS slot tokens observed in the recording: 0x0018 init,
    // 0x0020 code-open, 0x0014 open-file, 0x0010 get-size, 0x0028 read.
    // The server does not validate the slot against the command (our
    // 0x0020-everywhere opens/sizes worked), but match them where known.
    runtime.write32(pkt + BigInt(ctxAt), BigInt(options.token || CTX_TOKEN));
    runtime.write32(pkt + BigInt(ctxAt + 4), BigInt(options.object));
    const sfci = ctxAt + 0x10;
    runtime.write32(pkt + BigInt(sfci), 0x49434653n);      // SFCI
    runtime.write32(pkt + BigInt(sfci + 4), 1n);           // version
    runtime.write32(pkt + BigInt(sfci + 8), BigInt(options.cmd));
    if (input.length) writeNative(pkt + BigInt(sfci + 0x10), input);
    return sfci + 0x10;                             // data offset
}

// Send one WithContext request through the inspection domain. Returns
// {transport, result, handle, added, words} — `added` is the new domain
// object id (route follow-ups through it); `handle` is informational only.
export function sendContext(context, object, options) {
    const pkt = options.pkt;
    buildContextPacket(pkt, {...options, object});
    const transport = bareSendTransport(context.inspectionDomain.handle, pkt);
    const words = [];
    for (let i = 0n; i < 16n; i++)
        words.push(Number(runtime.read32(pkt + i * 4n)) >>> 0);
    if (transport !== 0)
        return {transport, result: -1, handle: 0, added: 0, words};
    let result = -1, handle = 0, added = 0;
    for (let i = 0; i < 15; i++) {
        if (words[i] === 0x4F434653) {              // SFCO
            result = words[i + 1];
            const h = words[i + 3];
            if (h > 0x10000000 && h < 0xffff8000) handle = h;
            const a = words[i + 4];
            if (a > 0 && a <= 0x4000) added = a;
            break;
        }
    }
    return {transport, result, handle, added, words};
}

export async function contextLog(tag, reply) {
    await khLog(tag + " t=" + hex(reply.transport) +
        " result=" + hex(reply.result === -1 ? 0xffffffff : reply.result) +
        " added=" + hex(reply.added) +
        " [" + reply.words.slice(0, 14).map(w => hex(w)).join(" ") + "]");
}
