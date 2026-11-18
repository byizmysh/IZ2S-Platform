const fs = require('node:fs/promises');
const path = require('node:path');
const crypto = require('node:crypto');
const {Session, live, check, u32} = require('./session.cjs');
const {nativeHash} = require('./filesystem.cjs');
const {verifyWorkerCode, code, SENTINEL} = require('./worker.cjs');
const native = require('./native-worker.cjs');
const events = require('./events.cjs');
const affinity = require('./affinity.cjs');
const timing = require('./timing.cjs');

async function main() {
  const session = new Session('qualify-' + Date.now(), 0x80000);
  const proof = {session: live.session, started: new Date().toISOString(), verified: false};
  let gate, worker;
  try {
    await session.initialize();
    await verifyWorkerCode(session); await events.verify(session);
    proof.frequency = await timing.verify(session);
    const allowed = await affinity.available(session);
    const source = crypto.randomBytes(0x10002), base = session.base;
    await session.write(base + 0x5000, source);
    check((await session.read(base + 0x5000, source.length)).equals(source), 'Multipart transfer differs');
    proof.transferSha256 = crypto.createHash('sha256').update(source).digest('hex');
    check(await nativeHash(session, base + 0x5000, source.length) === proof.transferSha256,
      'Multipart transfer failed its native hash check');
    await session.write(base + 0x4000, u32(64));
    await session.write(base + 0x70000, Buffer.alloc(80, 0xa5));
    gate = base + 0x3000;
    await events.initialize(session, gate);
    const calls = [
      {function: live.sdk + events.code.wait.offset, args: [gate, 0, 0, 0]},
      {function: live.sdk + code.current.offset, args: [0, 0, 0, 0]},
      affinity.currentCoreCall(),
      {function: live.sdk + 0x586fb0, args: [base + 0x70000, base + 0x5000, {u32: base + 0x4000}, 0]},
    ];
    worker = await native.start(session, base + 0x20000, 0x30000, base + 0x2000, calls, session.label);
    check((await session.readOwned(worker.base + worker.resultOffset, 8)).readBigUInt64LE() === SENTINEL,
      'Worker passed its start gate');
    const mask = allowed & -allowed;
    await affinity.pin(session, worker, mask);
    await events.signal(session, gate);
    await native.join(session, worker);
    const actual = await session.readOwned(base + 0x70000, 80);
    check(worker.results[1] === BigInt(worker.thread) && (1n << worker.results[2]) === mask &&
      worker.results[3] === BigInt(base + 0x70000) && actual.subarray(0, 64).equals(source.subarray(0, 64)) &&
      actual.subarray(64).every(byte => byte === 0xa5), 'Native worker identity, dynamic argument or copy differs');
    proof.worker = {thread: worker.thread, core: Number(worker.results[2]), joined: worker.joined};
    proof.verified = true;
  } catch (error) {proof.error = error.stack; throw error;}
  finally {
    if (await session.ready()) {
      if (worker && !worker.joined && session.threads.has(worker.thread)) {
        await events.signal(session, gate); await native.join(session, worker);
      }
      if (gate && !session.threads.size) await events.finalize(session, gate);
      await session.cleanup();
    }
    proof.cleanedUp = !session.base && !session.handles.size && !session.threads.size;
    proof.completed = new Date().toISOString();
    const file = path.join(__dirname, 'qualification.json');
    await fs.writeFile(file, JSON.stringify(proof, null, 2) + '\n', {mode: 0o600});
    console.log(JSON.stringify({file, ...proof}));
  }
}
if (require.main === module) main().catch(error => {console.error(error.stack); process.exitCode = 1;});
