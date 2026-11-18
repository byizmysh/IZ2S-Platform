// Variable-size original-SDK worker frames. All memory belongs to this Session.
const fs = require('node:fs/promises');
const path = require('node:path');
const crypto = require('node:crypto');
const {live, check} = require("./session.cjs");
const {nativeHash} = require("./filesystem.cjs");
const {code, SENTINEL} = require("./worker.cjs");
const {build} = require('../../shared/frames.cjs');

const align = n => Math.ceil(n / 4096) * 4096;
function capacity(calls) {
  const count = calls.reduce((n, call) => n + 1 + call.args.filter(a => typeof a === 'object').length, 0);
  return align(0x20000 + count * 0x300);
}

async function writeSparse(s, address, bytes) {
  check(address % 8 === 0 && bytes.length % 8 === 0 && address >= s.base && address + bytes.length <= s.base + s.bytes,
    'Sparse write is outside the owned allocation');
  // start() has already cleared the whole allocation. Send only occupied
  // pages; the native hash below still checks every byte, including zeroes.
  let first = null;
  for (let offset = 0; offset <= bytes.length; offset += 4096) {
    const occupied = offset < bytes.length && bytes.subarray(offset, offset + 4096).some(Boolean);
    if (occupied && first === null) first = offset;
    if (!occupied && first !== null) {
      await s.write(address + first, bytes.subarray(first, offset));
      first = null;
    }
  }
}

async function start(s, base, limit, slot, calls, label, snapshot = 0) {
  check(base % 4096 === 0 && base >= s.base && base + limit <= s.base + s.bytes, 'Worker frame is outside allocation');
  check(slot % 8 === 0 && slot >= s.base && slot + 16 <= base, 'Worker output slot overlaps its frame');
  const plan = path.join(__dirname, label + '-plan.json'), filename = path.join(__dirname, label + '-frame.bin');
  await fs.writeFile(plan, JSON.stringify({base, sdk: live.sdk, calls}) + '\n');
  const {frame, proof} = build(base, live.sdk, calls, code);
  await fs.writeFile(filename, frame, {flag: 'wx', mode: 0o600});
  check(frame.length === proof.bytes && frame.length <= limit, 'Worker frame exceeds reserved space');
  await s.call('memset', base, 0, limit);
  await writeSparse(s, base, frame);
  const hash = crypto.createHash('sha256').update(frame).digest('hex');
  check(await nativeHash(s, base, frame.length) === hash, 'Full worker frame hash differs');
  if (snapshot) {
    check(snapshot >= base + limit && snapshot + frame.length <= s.base + s.bytes, 'Worker snapshot is outside allocation');
    await s.rpc.call3(live.sdk + 0x586fb0, snapshot, base, frame.length);
    check(await nativeHash(s, snapshot, frame.length) === hash, 'Worker snapshot differs');
  }
  await s.record({operation: 'workerPrepared', label, base, slot, ...proof, frameSha256: hash});
  return launch(s, {label, base, slot, calls, ...proof, snapshot, frameSha256: hash});
}

async function launch(s, prepared) {
  const {label, base, slot} = prepared;
  await s.write(slot, Buffer.alloc(16));
  const pending = label + ':creating';
  s.threads.add(pending);
  const result = await s.rpc.call4(live.sdk + code.create.offset, slot, 0, live.sdk + code.longjmp.offset, base);
  if (result) s.threads.delete(pending);
  check(!result, 'pthread_create failed: ' + result);
  const thread = Number((await s.readOwned(slot, 8)).readBigUInt64LE());
  check(Number.isSafeInteger(thread) && thread > 0 && thread < 2 ** 39 && thread % 8 === 0, 'Invalid worker pointer');
  s.threads.delete(pending); s.threads.add(thread);
  const worker = {...prepared, thread, joined: false};
  check((await s.readOwned(base + worker.resultOffset, 8)).readBigUInt64LE() === SENTINEL, 'Worker passed its start gate');
  await s.record({operation: 'workerStarted', label, thread, base});
  return worker;
}

async function restart(s, worker) {
  // A joined thread's address can already belong to another worker.
  check(worker.snapshot && worker.joined, 'Cannot restore an active worker');
  await s.rpc.call3(live.sdk + 0x586fb0, worker.base, worker.snapshot, worker.bytes);
  delete worker.results;
  return launch(s, worker);
}

async function join(s, worker, timeoutMs = 15000) {
  check(!worker.joined && s.threads.has(worker.thread), 'Worker is not owned');
  const until = Date.now() + timeoutMs;
  let finished;
  do {
    finished = await s.rpc.call3(live.sdk + code.tryWait.offset, worker.thread, 0, 0);
    check(finished === 0 || finished === 1, 'Unexpected thread state');
    if (finished) break;
    await new Promise(resolve => setTimeout(resolve, 10));
  } while (Date.now() < until);
  check(finished, 'Native batch did not terminate; resources retained');
  const raw = await s.readOwned(worker.base + worker.resultOffsets[0], worker.resultOffsets.length * 8);
  const values = worker.resultOffsets.map((_, i) => raw.readBigUInt64LE(i * 8));
  check(values.every(value => value !== SENTINEL), 'Native batch did not store every result');
  check(!await s.rpc.call3(live.sdk + code.join.offset, worker.thread, worker.slot + 8, 0), 'pthread_join failed');
  s.threads.delete(worker.thread);
  worker.joined = true;
  check((await s.readOwned(worker.slot + 8, 8)).readBigUInt64LE() === values.at(-1), 'Thread exit result differs');
  worker.results = worker.logicalResultIndices.map(i => values[i]);
  await s.record({operation: 'workerJoined', label: worker.label, thread: worker.thread,
    values: worker.results.map(String), nativeCalls: values.length});
  return worker.results;
}

async function restartAll(s, workers) {
  for (const worker of workers) Object.assign(worker, await restart(s, worker));
  return workers;
}

async function joinAll(s, workers, timeoutMs = 15000) {
  for (const worker of workers) await join(s, worker, timeoutMs);
}

module.exports = {capacity, writeSparse, start, restart, join, restartAll, joinAll};
