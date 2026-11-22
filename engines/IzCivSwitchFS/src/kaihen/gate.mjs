// kaiHEN port — native request gating on a bound server session inside
// fsp-srv: park the session on the notifier handle, inject one-shot requests
// through the waiter array, and restore the session to its idle receive.

import {check, khLog, hex, u64Bytes, readU64LE, readU32LE, bytesEqual}
    from "./state.mjs";
import {readFs, writeFs, writeFsUnverified, compareWriteFs, fsU32, fsU64}
    from "./endpoint.mjs";
import {waitZero, eventSignal, eventClear, qualifyEventStubs} from "./pm.mjs";
import {cancelApiWorkers} from "./workers.mjs";
import {exportHandle} from "./objects.mjs";

function hexBytes(text) {
    const out = [];
    for (let i = 0; i + 1 < text.length; i += 2)
        out.push(parseInt(text.substr(i, 2), 16));
    return out;
}

export class Gate {
    constructor(context, row, serverCopy, event = null) {
        this.context = context;
        this.row = row;
        this.vptr = readU64LE(hexBytes(row.raw), 0);
        this.event = event;
        // Park target: the notifier session handle when a gate event exists,
        // otherwise the first API worker's source handle (reference gate.cjs).
        this.park = event ? event.sourceHandle : context.apiWorkers[0].sourceHandle;
        this.waiters = context.layout.waiters;
        check(this.waiters.length === 7 && this.waiters.every((address, i) =>
            address === this.waiters[0] + i * 0x110),
            "expected the verified contiguous native waiter array");
        this.serverCopy = serverCopy;
        this.steps = [];
        this.parked = false;
        this.outstanding = null;
    }

    async inspect(parked = true) {
        const b = await readFs(this.context, this.row.address, 0x88);
        check(readU64LE(b, 0) === this.vptr &&
            readU64LE(b, 0x70) === BigInt(this.row.domain) &&
            readU64LE(b, 0x78) === BigInt(this.row.metadata) &&
            readU32LE(b, 0x48) === this.row.receive &&
            readU64LE(b, 0x60) === BigInt(this.row.request) &&
            readU64LE(b, 0x68) === 0x100n,
            "the bound server session changed");
        check(readU32LE(b, 0x30) === (parked ? this.park : this.row.wait),
            "the wait word changed");
        // readU64LE yields BigInt; waiters are Numbers — coerce before the
        // includes() comparison (bindSessions does the same).
        return {parent: Number(readU64LE(b, 0x28)), state: readU32LE(b, 0x40),
            receiveResult: readU32LE(b, 0x44)};
    }

    async selections() {
        const bytes = await readFs(this.context,
            this.waiters[0] + 0x10, 6 * 0x110 + 8);
        return this.waiters.map((_, i) => readU64LE(bytes, i * 0x110));
    }

    async parkSession() {
        const before = await this.inspect(false);
        const beforeSel = await this.selections();
        check(before.state === 1 && this.waiters.includes(before.parent),
            "session is not idle on a known waiter (state=" + before.state +
            " parent=" + hex(before.parent) + " receiveResult=" +
            before.receiveResult + " waiters=[" +
            this.waiters.map(w => hex(w)).join(",") + "] selected=[" +
            beforeSel.map(w => hex(w)).join(",") + "] row=" +
            hex(this.row.address) + ")");
        await khLog("PK1 inspected");
        // Split compareWriteFs so a hang localizes to read/write/readback.
        const parkAddr = this.row.address + 0x30;
        const expect = u64Bytes(BigInt(this.row.wait)).slice(0, 4);
        const replace = u64Bytes(BigInt(this.park)).slice(0, 4);
        const beforePark = await readFs(this.context, parkAddr, 4);
        check(bytesEqual(beforePark, expect),
            "park word differs at " + hex(parkAddr));
        await khLog("PK1a read park word=" + hex(Number(readU32LE(beforePark, 0))));
        await writeFs(this.context, parkAddr, replace);
        await khLog("PK1b park word written");
        const afterPark = await readFs(this.context, parkAddr, 4);
        check(bytesEqual(afterPark, replace), "park readback differs");
        await khLog("PK1c park readback ok");
        this.parked = true;
        await khLog("PK2 park word written");
        cancelApiWorkers(this.context);
        await khLog("PK3 api workers cancelled");
        const after = await this.inspect(); // fresh ordinary FS request after cancellation
        await khLog("PK4 reinspected");
        check(after.state === 1 && this.waiters.includes(after.parent),
            "parked session did not rearm");
        // Native FS has separate wait queues. A null-selected cancellation
        // retries the same kernel wait array. Select the idle session once to
        // rebuild it.
        await khLog("PK5 stepping empty");
        await this.step({empty: true});
        await khLog("PK6 empty step done");
        return after;
    }

    // Server readiness: wait-zero on the exported server copy.
    async pending() {
        const result = waitZero(this.serverCopy);
        check(result === 0 || result === 0xea01, "server readiness check failed");
        return result === 0;
    }

    async step({empty = false} = {}) {
        check(this.parked && !this.outstanding,
            "gate is not parked or an earlier step is unresolved");
        const before = await this.inspect();
        check(before.state === 1 && this.waiters.includes(before.parent) &&
            await this.pending() === !empty,
            empty ? "an empty idle session is required to rebuild the wait array"
                : "a queued request on an idle parked session is required");
        const original = await readFs(this.context, this.row.request, 4);
        const step = this.outstanding = {empty,
            original: original.slice(), attempts: [], started: Date.now()};
        this.steps.push(step);
        await compareWriteFs(this.context, this.row.request,
            original.slice(), [0xff, 0xff, 0xff, 0xff]);
        for (let i = 0; i < 30; i++) {
            // A previous selection must settle before another pointer store.
            const selected = await this.selections();
            const session = await this.inspect();
            const epoch = this.waiters.includes(session.parent) ?
                (await fsU64(this.context, session.parent + 0x18)).toString() : null;
            const header = await fsU32(this.context, this.row.request);
            const injected = step.attempts.filter(a => a.injected).at(-1);
            if (empty && injected && !selected.includes(this.row.address) &&
                header === 0xffffffff && session.state === 1 &&
                session.receiveResult === 3 &&
                this.waiters.includes(session.parent) &&
                (session.parent !== injected.parent || epoch !== injected.epoch)) {
                step.rebuilt = {parent: session.parent, epoch,
                    receiveResult: session.receiveResult};
                await compareWriteFs(this.context, this.row.request,
                    [0xff, 0xff, 0xff, 0xff], original.slice());
                step.finished = Date.now();
                delete this.outstanding;
                return step;
            }
            if (header !== 0xffffffff && session.state === 1 &&
                this.waiters.includes(session.parent)) {
                step.request = (await readFs(this.context,
                    this.row.request, 0x100)).slice();
                step.finished = Date.now();
                delete this.outstanding;
                check(!empty, "an unexpected request arrived during the idle rebuild");
                return step;
            }
            if (header !== 0xffffffff || selected.includes(this.row.address) ||
                session.state !== 1 || !this.waiters.includes(session.parent)) {
                cancelApiWorkers(this.context);
                continue;
            }
            check(await this.pending() === !empty, "server readiness changed");
            const result = await this.injectHolder(session.parent, epoch);
            step.attempts.push(result);
        }
        throw new Error("KH: one-shot request did not settle");
    }

    // The reference retained-inject-holder: verify the parked session, write
    // the session address into the waiter's selection word, and immediately
    // cancel the API workers so the FS wait loop consumes the selection.
    async injectHolder(parent, epoch) {
        const waiter = await readFs(this.context, parent + 0x10, 0x18);
        const selected = readU64LE(waiter, 0);
        if (selected) return {injected: false, selected: Number(selected)};
        const session = await readFs(this.context, this.row.address, 0x88);
        check(readU64LE(session, 0) === this.vptr &&
            readU32LE(session, 0x30) === this.park &&
            readU64LE(session, 0x38) === 1n &&
            readU32LE(session, 0x48) === this.row.receive &&
            readU64LE(session, 0x60) === BigInt(this.row.request) &&
            readU64LE(session, 0x68) === 0x100n &&
            readU64LE(session, 0x70) === BigInt(this.row.domain) &&
            readU64LE(session, 0x78) === BigInt(this.row.metadata),
            "parked session identity changed");
        if (readU64LE(session, 0x28) !== BigInt(parent) ||
            readU32LE(session, 0x40) !== 1)
            return {injected: false, retry: true,
                parent: Number(readU64LE(session, 0x28)),
                state: readU32LE(session, 0x40)};
        const header = await fsU32(this.context, this.row.request);
        if (header !== 0xffffffff) return {injected: false, acknowledged: true, header};
        const entry = {injected: false, parent, epoch: readU64LE(waiter, 8).toString(),
            started: Date.now()};
        // Unverified write: a verified write's readback FS read can consume
        // the wake, so cancellation must immediately follow this store.
        await writeFsUnverified(this.context, parent + 0x10, u64Bytes(BigInt(this.row.address)));
        cancelApiWorkers(this.context);
        entry.injected = true;
        entry.finished = Date.now();
        return entry;
    }

    async restore() {
        if (!this.parked) return;
        const session = await this.inspect();
        check(session.state === 1 && this.waiters.includes(session.parent),
            "cannot restore an unsettled session");
        if (this.outstanding) {
            check(!(await this.selections()).includes(this.row.address),
                "a selection remains pending");
            const settled = await this.inspect();
            check(settled.state === 1 && this.waiters.includes(settled.parent),
                "the last injection has not settled");
            if (await fsU32(this.context, this.row.request) === 0xffffffff)
                await compareWriteFs(this.context, this.row.request,
                    [0xff, 0xff, 0xff, 0xff], this.outstanding.original.slice());
        }
        await compareWriteFs(this.context, this.row.address + 0x30,
            u64Bytes(BigInt(this.park)).slice(0, 4),
            u64Bytes(BigInt(this.row.wait)).slice(0, 4));
        if (this.event) {
            // Waking the old kernel wait object is safe even if the real
            // handle has already become selectable. No synthetic selection
            // or receipt marker is written after restoring the real handle.
            this.parked = false;
            const epoch = await fsU64(this.context, session.parent + 0x18);
            const signaled = eventSignal(this.context, this.event.write);
            check(signaled === 0, "restoration event did not signal");
            for (let i = 0; i < 30; i++) {
                const changed =
                    (await fsU64(this.context, session.parent + 0x18)) !== epoch;
                const current = await this.inspect(false);
                if (changed || current.state === 2 || current.state === 3 ||
                    current.parent !== session.parent) {
                    const cleared = eventClear(this.context, this.event.read);
                    check(cleared === 0, "restoration event did not clear");
                    this.restoration = {event: true, snapshotRetired: true};
                    return;
                }
            }
            throw new Error("KH: event wake did not retire the old wait snapshot");
        }
        const parkedHandle = this.park;
        this.park = this.row.wait;
        delete this.outstanding;
        cancelApiWorkers(this.context);
        await this.step({empty: !(await this.pending())});
        this.parked = false;
        this.park = parkedHandle;
        await this.inspect(false);
    }
}

// The gate event: the notifier's writable event exported into the game and
// the readable copy from GetEventHandle (reference prepare-event).
export async function prepareGateEvent(context) {
    const notifier = context.notifier;
    const b = await readFs(context, notifier.B + 0x20, 16);
    check(readU32LE(b, 4) === notifier.eventHandle &&
        readU32LE(b, 8) > 0x8000 && readU32LE(b, 8) !== notifier.eventHandle,
        "native SystemEvent layout changed");
    const copy = await exportHandle(context, readU32LE(b, 8),
        "owned notifier writable event");
    const event = {sourceHandle: notifier.eventHandle,
        read: notifier.readEvent, write: copy.handle};
    // sourceHandle must be the FS-SIDE SystemEvent handle (backend+0x24 —
    // the author's prepare-event.cjs parks on notifier.handle = that field).
    // Parking on the client session handle (a value in OUR process's table)
    // makes the fs server's waiter re-arm on an invalid handle and wedges
    // every fs session — runs 90-100 hung in the park write exactly so.
    // Signal/clear qualification through the syscall stubs; this also swaps
    // the scanned offsets if the first assignment is backwards.
    qualifyEventStubs(context, event);
    event.verified = true;
    context.gateEvent = event;
    await khLog("gate event verified read=" + hex(event.read) +
        " write=" + hex(event.write));
    return event;
}
