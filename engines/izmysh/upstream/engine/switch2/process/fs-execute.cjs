// Change one identified FS method, observe fresh native results, and restore it.
const fs = require('node:fs/promises');
const path = require('node:path');
const crypto = require('node:crypto');
const {Client, check, read} = require("./client.cjs");
const prefix = Buffer.from('7f2303d5ff8300d1fd7b01a9fd430091', 'hex');

function returnCode(value) {
  const code = Buffer.alloc(16);
  [0xd503245f, 0x52800000 | ((value & 0xffff) << 5),
    0x72a00000 | ((value >>> 16) << 5), 0xd65f03c0]
    .forEach((instruction, i) => code.writeUInt32LE(instruction >>> 0, i * 4));
  return code;
}

async function main() {
  const c = await new Client().open(), m = c.memory;
  const proof = {session: m.info.session, report: m.reportFile, protocolEvidence: c.file,
    started: new Date().toISOString(), trials: [], verified: false};
  const file = path.join(__dirname, proof.session + '-fs-execution-' + Date.now() + '.json');
  const save = () => fs.writeFile(file, JSON.stringify(proof, null, 2) + '\n', {mode: 0o600});
  await save();
  async function notifierCall(handle) {
    const reply = await c.worker({kind: 'notifier-probe', handle});
    for (const handle of [...(reply.copies || []), ...(reply.moves || [])])
      await m.request({command: 'root-native-close-handle', handle});
    return reply;
  }
  try {
    proof.processHandle = await c.fsProcess();
    proof.identity = await c.info(proof.processHandle); await save();
    check(!proof.identity.result && proof.identity.programId === '0400000000000000', 'Target is not native FS');
    const notifier = await read(path.join(__dirname, '../ro', proof.session + '-notifier-proof.json'));
    check(notifier.identityVerified && notifier.report === m.reportFile && await m.pointer(notifier.E + 8) === notifier.B,
      'The owned notifier identity changed');
    const state = await m.request({command: 'root-native-info'}), handle = state.notifiers[0];
    check(handle && state.worker.joined, 'Owned notifier or idle native worker is missing');
    const table = await m.pointer(notifier.E), target = await m.pointer(table + 4 * 8);
    proof.notifier = {object: notifier.E, backend: notifier.B, table, handle, target};
    proof.mapping = await c.info(proof.processHandle, target, proof.identity.pid);
    check(!proof.mapping.result && proof.mapping.memory.type === 3 && proof.mapping.memory.permissions === 4 &&
      proof.mapping.memory.attributes === 0, 'Expected the native FS execute-only code region');
    proof.original = await c.read(proof.processHandle, proof.identity.pid, target, 16); await save();
    check(!proof.original.result && proof.original.hex === prefix.toString('hex'), 'Native GetEventHandle entry differs');
    proof.baseline = await notifierCall(handle); await save();
    check(!proof.baseline.transport && !proof.baseline.result && proof.baseline.copies.length === 1,
      'The original method did not return its event handle');
    for (let i = 0; i < 2; i++) {
      let nonce;
      do { nonce = (crypto.randomInt(1, 0x1fff) << 9) | 0x1fe; }
      while (proof.trials.some(t => t.nonce === nonce));
      const code = returnCode(nonce), row = {nonce, codeHex: code.toString('hex'), pending: 'install'};
      proof.trials.push(row); await save();
      let changed = false;
      try {
        row.install = await c.patch(proof.processHandle, proof.identity.pid, target, prefix, code);
        changed = row.install.expectedMatched && row.install.mapResult === 0;
        await save();
        check(!row.install.result && row.install.readbackMatched && row.install.permissions === 3,
          'The native instruction patch did not complete');
        row.pending = 'invoke'; await save();
        row.reply = await notifierCall(handle); await save();
      } finally {
        if (changed && !c.pending) {
          row.pending = 'restore'; await save();
          row.restore = await c.patch(proof.processHandle, proof.identity.pid, target, code, prefix); await save();
          check(!row.restore.result && row.restore.readbackMatched && !row.restore.unmapResult,
            'Original FS instructions were not restored');
          row.restored = true; delete row.pending; await save();
        }
      }
      check(!row.reply.transport && row.reply.canonicalResult === nonce && !row.reply.copies.length && !row.reply.moves.length,
        'The native FS reply did not contain the fresh instruction-generated value');
      row.readback = await c.read(proof.processHandle, proof.identity.pid, target, 16);
      check(!row.readback.result && row.readback.hex === prefix.toString('hex'), 'Restored instruction readback differs');
      row.control = await notifierCall(handle);
      check(!row.control.transport && !row.control.result && row.control.copies.length === 1,
        'The restored method did not resume its original behavior');
      row.verified = true; await save();
    }
    proof.verified = true; proof.finished = new Date().toISOString(); await save();
    const result = {verified: true, session: proof.session, pid: proof.identity.pid,
      programId: proof.identity.programId, trials: proof.trials.length, restored: true, file};
    console.log(JSON.stringify(result));
    return result;
  } catch (error) { proof.error = error.stack; await save(); error.proofFile = file; throw error; }
  finally { await c.close(); }
}
module.exports = {main, returnCode};
if (require.main === module) main().catch(error => {
  console.error(JSON.stringify({error: error.message, file: error.proofFile})); process.exitCode = 1;
});
