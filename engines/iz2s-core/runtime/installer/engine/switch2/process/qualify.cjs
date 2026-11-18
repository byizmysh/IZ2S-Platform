// Prove mapping, native instruction execution, and complete removal of the alias.
const fs = require('node:fs/promises');
const path = require('node:path');
const crypto = require('node:crypto');
const {Client, check, read} = require("./client.cjs");

async function main() {
  const client = await new Client().open(), memory = client.memory;
  const proof = {session: memory.info.session, report: memory.reportFile, roPid: client.pid,
    protocolEvidence: client.file, started: new Date().toISOString(), verified: false};
  const file = path.join(__dirname, proof.session + '-qualification-' + Date.now() + '.json');
  const save = () => fs.writeFile(file, JSON.stringify(proof, null, 2) + '\n', {mode: 0o600});
  let localPending = false;
  await save();
  try {
    proof.app = await client.info(0xffff8001); await save();
    check(!proof.app.result && ['0100c9100b06a000', '0100b4b00f9f6000'].includes(proof.app.programId),
      'The transferred current-process handle is not SmileBASIC');
    const fsHandle = await client.fsProcess();
    proof.fs = await client.info(fsHandle); await save();
    check(!proof.fs.result && proof.fs.programId === '0400000000000000', 'The exported handle is not native FS');
    const layout = await read(path.join(__dirname, '../ro', proof.session + '-layout.json'));
    proof.fsRodata = await client.info(fsHandle, layout.rootVptr, proof.fs.pid); await save();
    proof.fsRodataRead = await client.read(fsHandle, proof.fs.pid, layout.rootVptr, 32); await save();
    if (!proof.fsRodataRead.result) {
      const expected = await memory.read(layout.rootVptr, 32);
      check(expected.toString('hex') === proof.fsRodataRead.hex, 'RO mapping disagrees with the retained FS reader');
      proof.fsRodataRead.matchesRetainedReader = true; await save();
    }
    const previousPage = BigInt(proof.fsRodata.memory.address) - 0x1000n;
    proof.fsText = await client.info(fsHandle, previousPage, proof.fs.pid); await save();
    if (!proof.fsText.result && proof.fsText.memory.permissions & 4) {
      proof.fsTextRead = await client.read(fsHandle, proof.fs.pid, proof.fsText.memory.address, 128); await save();
    }
    const spl = await client.service('spl:');
    proof.splSessionAsProcess = await client.info(spl); await save();
    proof.prepared = await memory.request({command: 'root-native-process-test-prepare'}); await save();
    const destination = proof.app.freeAddress;
    check(BigInt(destination) > 0n && BigInt(proof.app.roFreeAddress) > 0n, 'No qualified free virtual address');
    proof.map = await client.map(0xffff8001, proof.app.pid, proof.prepared.source, destination); await save();
    check(!proof.map.result, 'Native code mapping failed: ' + proof.map.result.toString(16));
    proof.mapped = await client.info(0xffff8001, destination, proof.app.pid); await save();
    check(!proof.mapped.result && proof.mapped.memory.permissions === 5, 'The target alias is not RX');
    const original = (await fs.readFile(path.join(__dirname, 'test-function.bin'))).subarray(0, 16);
    proof.cacheMaintenance = await client.patch(0xffff8001, proof.app.pid, destination, original, original); await save();
    check(!proof.cacheMaintenance.result && proof.cacheMaintenance.readbackMatched, 'Native writable alias/cache-maintenance check failed');
    proof.executions = [];
    for (let i = 0; i < 2; i++) {
      const nonce = crypto.randomInt(1, 0x40000000);
      localPending = true;
      const executed = await memory.request({command: 'root-native-process-test-execute', destination, nonce});
      localPending = false;
      proof.executions.push(executed); await save();
      const out = Buffer.from(executed.outputHex, 'hex'), transformed = nonce ^ (nonce << 1);
      check(out.readUInt32LE(0) === 0 && out.readBigUInt64LE(8) === BigInt(proof.app.pid), 'Native code reported the wrong process identity');
      check(out.readUInt32LE(16) === 0 && out.readBigUInt64LE(24) > 0n, 'Native thread identity query failed');
      check(out.readBigUInt64LE(40) === BigInt(transformed) && executed.result === transformed, 'Fresh native nonce calculation differs');
      if (i) check(out.readBigUInt64LE(32) > Buffer.from(proof.executions[0].outputHex, 'hex').readBigUInt64LE(32), 'Native system tick did not advance');
    }
    proof.unmap = await client.unmap(0xffff8001, proof.app.pid, proof.prepared.source, destination); await save();
    check(!proof.unmap.result, 'The test alias did not unmap');
    proof.cleanup = await memory.request({command: 'root-native-process-test-cleanup', destination});
    proof.verified = true; proof.finished = new Date().toISOString(); await save();
    const result = {verified: true, session: proof.session, roPid: client.pid,
      targetPid: proof.app.pid, targetProgramId: proof.app.programId, fsReadResult: proof.fsRodataRead.result, file};
    console.log(JSON.stringify(result));
    return result;
  } catch (error) {
    proof.error = error.stack; await save();
    if (!client.pending && !localPending && proof.prepared && !proof.cleanup) {
      try {
        if (proof.map && !proof.map.result && !proof.unmap) {
          proof.unmap = await client.unmap(0xffff8001, proof.app.pid, proof.prepared.source, proof.app.freeAddress);
          await save();
        }
        proof.cleanup = await memory.request({command: 'root-native-process-test-cleanup', destination: proof.app.freeAddress});
        await save();
      } catch (cleanupError) { proof.cleanupError = cleanupError.stack; await save(); }
    }
    error.proofFile = file; throw error;
  } finally { await client.close(); }
}
module.exports = {main};
if (require.main === module) main().catch(error => {
  console.error(JSON.stringify({error: error.message, file: error.proofFile})); process.exitCode = 1;
});
