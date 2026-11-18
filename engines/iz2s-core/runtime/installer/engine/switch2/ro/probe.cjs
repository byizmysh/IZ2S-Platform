// Two fresh challenges, attributed to the server by the kernel's PID descriptor.
const fs = require('node:fs/promises');
const path = require('node:path');
const crypto = require('node:crypto');
const {connect} = require("../tools/fs-memory.cjs");
const check = (ok, message) => { if (!ok) throw Error(message); };

async function main(pid) {
  check(typeof pid === 'string' && /^\d+$/.test(pid), 'Expected the accepted RO process ID');
  const name = 'ro:dmnt';
  const memory = await connect();
  const state = await memory.request({command: 'root-native-info'});
  const proof = {session: memory.info.session, report: memory.reportFile, pid, name,
    started: new Date().toISOString(), challenges: [], verified: false};
  const file = path.join(__dirname, memory.info.session + '-execution-proof-' + Date.now() + '.json');
  const save = () => fs.writeFile(file, JSON.stringify(proof, null, 2) + '\n', {mode: 0o600});
  await save();
  async function worker(request) {
    await memory.request({command: 'root-native-worker-prepare', ...request});
    await memory.request({command: 'root-native-worker-release'});
    const until = Date.now() + 4000;
    for (;;) {
      const status = await memory.request({command: 'root-native-worker-state'});
      if (status.finished) return memory.request({command: 'root-native-worker-join'});
      check(Date.now() < until, 'Proof worker remains pending; preserved without issuing another synchronous IPC');
      await new Promise(resolve => setTimeout(resolve, 25));
    }
  }
  try {
    const handle = state.services.find(s => s.name === 'pm:shell')?.sm;
    check(handle, 'The borrowed FS SM client is missing');
    proof.lookup = await worker({kind: 'sm-get', handle, name});
    await save();
    const lookup = proof.lookup.responses[0];
    check(!lookup.transport && !lookup.result && lookup.handle, 'The service lookup failed');
    proof.handle = lookup.handle;
    let previousTick = 0n;
    for (let i = 0; i < 2; i++) {
      const nonce = crypto.randomBytes(8);
      const row = {nonce: nonce.toString('hex')};
      proof.challenges.push(row); await save();
      row.reply = await worker({kind: 'ro-probe', handle: proof.handle, hex: row.nonce});
      await save();
      const reply = row.reply.responses[0];
      check(!reply.transport && !reply.result && reply.kernelPid === pid, 'The challenge did not return from the expected kernel PID');
      const data = Buffer.from(reply.data, 'hex');
      check(data.length >= 48 && data.readBigUInt64LE() === 0x4558454f5232534en &&
        data.readBigUInt64LE(8) === (nonce.readBigUInt64LE() ^ 0x9d7c3a51e6b2048fn) &&
        data.readBigUInt64LE(16) === BigInt(pid), 'The native payload response differs');
      row.threadId = data.readBigUInt64LE(24).toString();
      row.tick = data.readBigUInt64LE(32).toString();
      row.registration = data.subarray(40, 48).toString('hex');
      check(BigInt(row.tick) > previousTick, 'The native tick did not advance');
      previousTick = BigInt(row.tick);
      row.verified = true; await save();
    }
    proof.verified = true;
    proof.completed = new Date().toISOString();
    await save();
    proof.closed = await memory.request({command: 'root-native-close-handle', handle: proof.handle});
    await save();
    return {file, verified: true, pid, challenges: proof.challenges.length, completed: proof.completed};
  } catch (error) {
    proof.error = error.stack; await save();
    error.proofFile = file;
    throw error;
  }
}
module.exports = {main};
