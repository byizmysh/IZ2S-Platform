// Native request gating. The wait word is distinct from the receive handle.
const check = (ok, message) => { if (!ok) throw Error(message); };
const u32 = n => { const b = Buffer.alloc(4); b.writeUInt32LE(n); return b; };
const pointer = (b, o) => Number(b.readBigUInt64LE(o));
const WAKE_SETTLE_MS = 100;

// Keep the event asserted only for a bounded quiet wake interval.
// Retained FS reads must not run until the event has been cleared: a repeatedly
// selected event can starve the FS request used to inspect the waiter epoch.
async function wakeAndClear(memory, event, restoration) {
  let failure;
  try {
    const signaled = await memory.request({command: 'root-native-event-signal', handle: event.write});
    restoration.signal = {at: signaled.at, result: signaled.result};
    check(signaled.result === 0, 'Restoration event did not signal');
    await new Promise(resolve => setTimeout(resolve, WAKE_SETTLE_MS));
  } catch (error) {
    failure = error;
    restoration.wakeError = error.message;
  } finally {
    try {
      const cleared = await memory.request({command: 'root-native-event-clear', handle: event.read});
      restoration.clear = {at: cleared.at, result: cleared.result};
      check(cleared.result === 0, 'Restoration event did not clear');
    } catch (error) {
      restoration.clearError = error.message;
      if (failure) failure.clearError = error.message;
      else failure = error;
    }
  }
  if (failure) throw failure;
}

class Gate {
  constructor(memory, row, workers, waiters, serverCopy, event = null) {
    this.memory = memory;
    this.row = row;
    this.vptr = pointer(Buffer.from(row.raw, 'hex'), 0);
    this.event = event;
    this.park = event ? event.sourceHandle : workers[0].sourceHandle;
    this.waiters = waiters;
    check(waiters.length === 7 && waiters.every((address, i) => address === waiters[0] + i * 0x110),
      'Expected the verified contiguous native waiter array');
    this.serverCopy = serverCopy;
    this.steps = [];
  }

  async inspect(parked = true) {
    const b = await this.memory.read(this.row.address, 0x88);
    check(pointer(b, 0) === this.vptr && pointer(b, 0x70) === this.row.domain &&
      pointer(b, 0x78) === this.row.metadata && b.readUInt32LE(0x48) === this.row.receive &&
      pointer(b, 0x60) === this.row.request && pointer(b, 0x68) === 0x100,
    'The bound server session changed');
    check(b.readUInt32LE(0x30) === (parked ? this.park : this.row.wait), 'The wait word changed');
    return {parent: pointer(b, 0x28), state: b.readUInt32LE(0x40),
      receiveResult: b.readUInt32LE(0x44), raw: b.toString('hex')};
  }

  async cancel() { return this.memory.request({command: 'root-native-worker-cancel'}); }

  async selections() {
    const bytes = await this.memory.read(this.waiters[0] + 0x10, 6 * 0x110 + 8);
    return this.waiters.map((_, i) => pointer(bytes, i * 0x110));
  }

  async parkSession() {
    const before = await this.inspect(false);
    check(before.state === 1 && this.waiters.includes(before.parent), 'Session is not idle on a known waiter');
    await this.memory.write(this.row.address + 0x30, u32(this.row.wait), u32(this.park));
    this.parked = true;
    await this.cancel();
    const after = await this.inspect(); // fresh ordinary FS request after cancellation
    check(after.state === 1 && this.waiters.includes(after.parent), 'Parked session did not rearm');
    // Native FS has separate wait queues. A null-selected cancellation retries
    // the same kernel wait array. Select the idle session once to rebuild it.
    await this.step({empty: true});
    return after;
  }

  async pending() {
    const reply = await this.memory.request({command: 'root-native-wait-zero', handle: this.serverCopy});
    check(reply.result === 0 || reply.result === 0xea01, 'Server readiness check failed');
    return reply.result === 0;
  }

  async step({empty = false} = {}) {
    check(this.parked && !this.outstanding, 'Gate is not parked or an earlier step is unresolved');
    const before = await this.inspect();
    check(before.state === 1 && this.waiters.includes(before.parent) && await this.pending() === !empty,
      empty ? 'An empty idle session is required to rebuild the wait array' : 'A queued request on an idle parked session is required');
    const original = await this.memory.read(this.row.request, 4);
    const step = this.outstanding = {empty, original: original.toString('hex'), attempts: [], started: new Date().toISOString()};
    this.steps.push(step);
    await this.memory.write(this.row.request, original, u32(0xffffffff));
    for (let i = 0; i < 30; i++) {
      // A previous selection must settle before another pointer store.
      const selected = await this.selections();
      const session = await this.inspect();
      const epoch = this.waiters.includes(session.parent) ? (await this.memory.u64(session.parent + 0x18)).toString() : null;
      const header = await this.memory.u32(this.row.request);
      const injected = step.attempts.filter(a => a.injected).at(-1);
      if (empty && injected && !selected.includes(this.row.address) && header === 0xffffffff &&
        session.state === 1 && session.receiveResult === 3 && this.waiters.includes(session.parent) &&
        (session.parent !== injected.parent || epoch !== injected.epoch)) {
        step.rebuilt = {parent: session.parent, epoch, receiveResult: session.receiveResult};
        await this.memory.write(this.row.request, u32(0xffffffff), original);
        step.finished = new Date().toISOString();
        delete this.outstanding;
        return step;
      }
      if (header !== 0xffffffff && session.state === 1 && this.waiters.includes(session.parent)) {
        step.request = (await this.memory.read(this.row.request, 0x100)).toString('hex');
        step.finished = new Date().toISOString();
        delete this.outstanding;
        check(!empty, 'An unexpected request arrived during the idle rebuild');
        return step;
      }
      if (header !== 0xffffffff || selected.includes(this.row.address) || session.state !== 1 ||
        !this.waiters.includes(session.parent)) {
        await this.cancel();
        continue;
      }
      check(await this.pending() === !empty, 'Server readiness changed before receipt');
      const result = await this.memory.request({command: 'retained-inject-holder',
        server: this.row.address, parent: session.parent, vptr: this.vptr,
        park: this.park, receive: this.row.receive, buffer: this.row.request,
        domain: this.row.domain, metadata: this.row.metadata});
      step.attempts.push(result);
    }
    throw Error('One-shot request did not settle; inspect the saved step before retrying');
  }

  async restore() {
    if (!this.parked) return;
    const session = await this.inspect();
    check(session.state === 1 && this.waiters.includes(session.parent), 'Cannot restore an unsettled session');
    if (this.outstanding) {
      check(!(await this.selections()).includes(this.row.address), 'A selection remains pending');
      const settled = await this.inspect();
      check(settled.state === 1 && this.waiters.includes(settled.parent), 'The last injection has not settled');
      if (await this.memory.u32(this.row.request) === 0xffffffff)
        await this.memory.write(this.row.request, u32(0xffffffff), Buffer.from(this.outstanding.original, 'hex'));
    }
    await this.memory.write(this.row.address + 0x30, u32(this.park), u32(this.row.wait));
    if (this.event) {
      // No synthetic selection or receipt marker is written after restoring
      // the real handle. A fresh FS read must prove the old snapshot retired.
      this.parked = false;
      const epoch = await this.memory.u64(session.parent + 0x18);
      const restoration = this.restoration = {event: true, snapshotRetired: false,
        epochBefore: epoch.toString(), wakeSettleMs: WAKE_SETTLE_MS};
      await wakeAndClear(this.memory, this.event, restoration);
      for (let i = 0; i < 30; i++) {
        const currentEpoch = await this.memory.u64(session.parent + 0x18);
        const current = await this.inspect(false);
        if (currentEpoch !== epoch || current.state === 2 || current.state === 3 ||
            current.parent !== session.parent) {
          Object.assign(restoration, {snapshotRetired: true, epochAfter: currentEpoch.toString(),
            state: current.state, parent: current.parent, checks: i + 1});
          return;
        }
      }
      throw Error('Event wake did not retire the old wait snapshot');
    }
    const parkedHandle = this.park;
    this.park = this.row.wait;
    delete this.outstanding;
    await this.cancel();
    await this.step({empty: !await this.pending()});
    this.parked = false;
    this.park = parkedHandle;
    await this.inspect(false);
  }
}

// Sessions sharing one parking event must all leave it before it is signaled.
// Otherwise a still-parked session can repeatedly wake and starve FS requests.
async function restoreGates(gates) {
  const parked = gates.filter(gate => gate.parked);
  if (!parked.length) return;
  if (parked.length === 1) return parked[0].restore();
  const first = parked[0], memory = first.memory, event = first.event;
  check(event && parked.every(gate => gate.memory === memory &&
    gate.event?.sourceHandle === event.sourceHandle && gate.event?.read === event.read &&
    gate.event?.write === event.write && !gate.outstanding),
  'Group restoration requires settled sessions sharing one parking event');
  const sessions = [];
  for (const gate of parked) {
    const session = await gate.inspect();
    check(session.state === 1 && gate.waiters.includes(session.parent), 'A grouped session has not settled');
    sessions.push(session);
  }
  for (const gate of parked)
    await memory.write(gate.row.address + 0x30, u32(gate.park), u32(gate.row.wait));
  const epochs = [];
  for (const session of sessions) epochs.push(await memory.u64(session.parent + 0x18));
  for (const gate of parked) gate.parked = false;
  const restoration = {event: true, snapshotRetired: false, grouped: parked.length,
    epochBefore: epochs.map(epoch => epoch.toString()), wakeSettleMs: WAKE_SETTLE_MS};
  for (const gate of parked) gate.restoration = restoration;
  await wakeAndClear(memory, event, restoration);
  for (let i = 0; i < 30; i++) {
    const snapshots = [];
    for (let n = 0; n < parked.length; n++) {
      const current = await parked[n].inspect(false);
      const currentEpoch = await memory.u64(sessions[n].parent + 0x18);
      snapshots.push({row: parked[n].row.address, parentBefore: sessions[n].parent,
        parentAfter: current.parent, epochBefore: epochs[n].toString(),
        epochAfter: currentEpoch.toString(), state: current.state,
        retired: currentEpoch !== epochs[n] || current.parent !== sessions[n].parent ||
          current.state === 2 || current.state === 3});
    }
    restoration.snapshots = snapshots;
    if (snapshots.every(snapshot => snapshot.retired)) {
      Object.assign(restoration, {snapshotRetired: true, checks: i + 1});
      return;
    }
  }
  throw Error('Grouped restoration did not retire the old wait snapshot');
}

module.exports = {Gate, restoreGates};
