// kaiHEN port — FS-process layout: SDK base, domain table walk, Loader root
// entry, server-session pool, waiter array, and the native thread ring.
//
// civ7-fs-kaihen derives this from a heap capture; CivSwitchFS's discovery
// already verified the same manager/table/rootVptr live, so this port
// re-derives the remaining anchors from that layout with live checks.

import {check, khLog, hex, readU64LE, readU32LE} from "./state.mjs";
import {readFs, fsPointer, fsU32} from "./endpoint.mjs";

// The SDK page sits at a fixed offset from the retained-capture base — the
// same anchor the reference uses (layout.cjs: sdk = range.address + 0x500000;
// CivSwitchFS config enforces captureEnd = target - 0x28000, i.e. the capture
// is the reference's 0x4D8000 retained window ending one 0x28000 bucket before
// the SDK page). The rootVptr + 0xf38be0 distance is NOT stable: it shifts by
// 0x4000 between boots (observed live on boot 11: seed landed one page below
// the true SDK page, inside the unmapped bucket gap — the read faulted the FS
// server and wedged the eval in runs 3 and 4). Only the capture anchor holds.
const SDK_FROM_CAPTURE_START = 0x500000;
const HOLDER_FROM_ROOT_VPTR = 0x7d78;
const SLOTS_FROM_MANAGER = 0xd48;
const WAITERS_FROM_MANAGER = 0x410;
const WAITER_STRIDE = 0x110;

export async function deriveLayout(context) {
    const base = context.state.domainLayout;
    check(base && Number.isSafeInteger(base.table) && Number.isSafeInteger(base.manager) &&
        Number.isSafeInteger(base.rootVptr) && Number.isSafeInteger(base.object),
        "domain layout snapshot is incomplete");
    const layout = {
        manager: base.manager, table: base.table, count: 0x4000,
        rootVptr: base.rootVptr,
        captureStart: base.captureStart, captureEnd: base.captureEnd,
        sdk: base.captureStart + SDK_FROM_CAPTURE_START,
        holderVptr: base.rootVptr - HOLDER_FROM_ROOT_VPTR,
        slots: base.manager + SLOTS_FROM_MANAGER,
        waiters: Array.from({length: 7},
            (_, i) => base.manager + WAITERS_FROM_MANAGER + i * WAITER_STRIDE)
    };

    // Live identity checks (reference layout.validate), each preceded by a
    // phase marker: if the next run wedges, the last marker names the IPC.
    await khLog("DL1 manager read");
    const manager = await readFs(context, layout.manager, 0x20);
    check(readU32LE(manager, 0x10) === layout.count &&
        readU64LE(manager, 0x18) === BigInt(layout.table),
        "live domain manager changed");
    await khLog("DL2 entry read");
    const ourEntry = await readFs(context, base.entry, 0x30);
    check(readU64LE(ourEntry, 0x18) === BigInt(base.domain) &&
        readU64LE(ourEntry, 0x20) === BigInt(base.object),
        "our root domain entry changed");
    await khLog("DL3 domain read");
    const domain = await readFs(context, base.domain, 0x20);
    check(readU64LE(domain, 0) === BigInt(layout.rootVptr + 0x28e0) &&
        readU64LE(domain, 8) === BigInt(layout.rootVptr + 0x2938) &&
        readU64LE(domain, 0x18) === BigInt(layout.manager),
        "our domain session object identity changed");

    // SDK page sanity: thread-ring header at sdk+0x830 (reference sdkPageHeader).
    // Narrow 0x20 read: only this window is checked, and a smaller read keeps
    // the first touch of the SDK page to one IPC.
    await khLog("DL4 sdk ring header read");
    const ring = await readFs(context, layout.sdk + 0x830, 0x20);
    check(readU64LE(ring, 8) === BigInt(layout.sdk + 0x668) &&
        readU64LE(ring, 0) >= 2n ** 32n && readU64LE(ring, 0) < 2n ** 39n &&
        readU32LE(ring, 0x18) > 0 && readU32LE(ring, 0x18) <= 128,
        "FS SDK page does not contain the expected thread-ring header");
    await khLog("DL5 layout verified (next step might take a bit to show)");

    context.layout = layout;
    await khLog("layout sdk=" + hex(layout.sdk) + " table=" + hex(layout.table) +
        " holderVptr=" + hex(layout.holderVptr) + " slots=" + hex(layout.slots));
    return layout;
}

// Locate the Loader's fsp-srv root entry: the unique table entry whose
// metadata matches the filesystem-root vtable family and whose owning domain
// is a domain-session object on the verified manager.
export async function findLoaderRoot(context) {
    const layout = context.layout;
    // Pass 1 — scan the verified capture window for LIVE domain-session
    // objects (vtable pair, manager at +0x18) and root-family objects. Every
    // read stays inside the window, mapped by construction, so a stale table
    // entry can never lure us into an unmapped dereference (boots 15/17
    // crashed exactly there: first-page metadata matches with dangling
    // pointers faulted the FS server and killed the game).
    const domainObjects = new Set();
    const rootObjects = new Set();
    const domainVptr1 = BigInt(layout.rootVptr + 0x28e0);
    const domainVptr2 = BigInt(layout.rootVptr + 0x2938);
    const rootVptrWanted = BigInt(layout.rootVptr + 0x1ea0);
    const managerWanted = BigInt(layout.manager);
    for (let at = layout.captureStart; at < layout.captureEnd; at += 0x4000) {
        const length = Math.min(0x4000, layout.captureEnd - at);
        const bytes = await readFs(context, at, length);
        for (let i = 0; i + 0x20 <= length; i += 8) {
            const first = readU64LE(bytes, i);
            if (first === domainVptr1 &&
                    readU64LE(bytes, i + 8) === domainVptr2 &&
                    readU64LE(bytes, i + 0x18) === managerWanted)
                domainObjects.add(at + i);
            else if (first === rootVptrWanted)
                rootObjects.add(at + i);
        }
    }
    await khLog("FR0 window scan: " + domainObjects.size +
        " domain objects, " + rootObjects.size + " root-family objects");
    // Pass 2 — scan the table's first page for metadata matches (reference
    // parity: captured(table, 0x4000); reading further faulted the FS server
    // on boot 4 run 34 — the capacity 0x4000 counts entries but the live
    // allocation is not that large). Stale entries are filtered by the
    // live-owner check below instead of by scanning further.
    const metadataMatches = [];
    {
        const tableBytes = await readFs(context, layout.table, 0x4000);
        for (let i = 0; i + 0x30 <= 0x4000; i += 0x30) {
            const owner = readU64LE(tableBytes, i + 0x18);
            const object = readU64LE(tableBytes, i + 0x20);
            const metadata = readU64LE(tableBytes, i + 0x28);
            if (owner === 0n || object === 0n) continue;
            if (metadata === BigInt(layout.rootVptr + 0x5880))
                metadataMatches.push({index: i / 0x30 + 1,
                    entry: layout.table + i,
                    owner: Number(owner), object: Number(object)});
        }
    }
    await khLog("FR1 table scan, " + metadataMatches.length + " pre-candidates");
    for (const candidate of metadataMatches)
        await khLog("FR1 candidate #" + candidate.index + " entry=" +
            hex(candidate.entry) + " owner=" + hex(candidate.owner) +
            " object=" + hex(candidate.object));
    check(metadataMatches.length >= 1, "no filesystem-root entries in the table");
    const accepted = [];
    for (const candidate of metadataMatches) {
        // Trust a candidate only when its owner is one of the live window
        // domain objects — no wild dereferences of table-derived pointers.
        if (!domainObjects.has(candidate.owner)) continue;
        // Object identity without dereferencing: inside the window the scan
        // already established it; outside, the live domain owner vouches for
        // the entry and copyLoaderRoot's before-reads validate the object.
        if (candidate.object >= layout.captureStart &&
                candidate.object < layout.captureEnd)
            check(rootObjects.has(candidate.object),
                "in-window candidate object lacks the root-family vptr");
        accepted.push(candidate);
    }
    check(accepted.length === 1,
        "Loader root is not unique: " + accepted.length + " accepted of " +
        metadataMatches.length + " candidates");
    const target = accepted[0];
    target.domain = target.owner;
    context.loaderRoot = target;
    await khLog("FR2 loader root entry=" + hex(target.entry) +
        " object=" + hex(target.object) + " domain=" + hex(target.domain));
    return target;
}

// Walk the FS thread ring starting at the sdk+0x830 sentinel.
export async function readThreadRing(context) {
    const layout = context.layout;
    await khLog("TR1 ring walk start");
    const sentinel = layout.sdk + 0x830;
    const nodes = [];
    const seen = {};
    let at = await fsPointer(context, sentinel);
    while (at !== sentinel) {
        check(at && !seen[at] && nodes.length < 128, "thread ring did not terminate");
        seen[at] = true;
        const b = await readFs(context, at, 0x1c0);
        nodes.push({
            address: at, next: readU64LE(b, 0), state: b[0x40], aliased: b[0x41],
            originalStack: readU64LE(b, 0x48), stack: readU64LE(b, 0x50),
            size: readU64LE(b, 0x58), argument: readU64LE(b, 0x60),
            entry: readU64LE(b, 0x68), handle: readU32LE(b, 0x1b0),
            id: readU64LE(b, 0x1b8)
        });
        at = Number(readU64LE(b, 0));
    }
    context.threadRing = nodes;
    await khLog("thread ring: " + nodes.length + " nodes");
    return nodes;
}
