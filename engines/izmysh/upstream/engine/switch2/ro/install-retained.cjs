// Promote an already captured, current-boot cache into the existing two-bucket interface.
const fs = require('node:fs/promises');
const path = require('node:path');
const crypto = require('node:crypto');
const memory = require('../tools/endpoint.cjs');
const check = (ok, message) => { if (!ok) throw Error(message); };
const {unusedTail} = require("../retained/cache-shape.cjs");
const rpc = require('../../shared/rpc.cjs');

async function main() {
  const currentFile = path.join(__dirname, '../current-acquisition.json');
  const current = JSON.parse(await fs.readFile(currentFile));
  const boot = {...rpc.a, ...await rpc.request({command: 'status'})};
  let report = JSON.parse(await fs.readFile(current.report));
  check(boot.ready && boot.platform === 2 && boot.session === report.session && current.session === boot.session,
    'The endpoint and current boot differ');
  const socket = report.memoryControl.socketPath;
  if (report.retainedMemory?.ready) { console.log(await memory.request(socket, {command: 'retained-info'})); return; }
  const state = report.retainedMemory;
  check(state && !state.promoted && !state.pending, 'Expected an idle captured bootstrap');
  const file = path.join(__dirname, boot.session + '-retained-install.json');
  async function finish(proof) {
    const save = () => fs.writeFile(file, JSON.stringify(proof, null, 2) + '\n', {mode: 0o600});
    proof.installation = await memory.request(socket, {command: 'retained-install', array: proof.array, owner: proof.owner});
    await save();
    proof.info = await memory.request(socket, {command: 'retained-info'});
    check(proof.info.ready && proof.info.steadyStateReliable && !proof.info.requiresCorruption,
      'Retained endpoint did not qualify');
    await save();
    current.phase = 'retained'; current.steadyStateReliable = true; current.retainedProof = file;
    await fs.writeFile(currentFile, JSON.stringify(current, null, 2) + '\n', {mode: 0o600});
    console.log(JSON.stringify({file, ...proof.info}));
  }
  const previous = await fs.readFile(file).then(JSON.parse).catch(e => { if (e.code !== 'ENOENT') throw e; });
  if (previous?.report === current.report && previous.attempts.at(-1)?.verified &&
      state.bootstrapPlan.target === previous.array) return finish(previous);
  const capture = state.captures.find(item => item.length === 0x4d8000);
  check(capture, 'Run find-metadata.cjs for this boot first');
  const bytes = await fs.readFile(capture.outputFile);
  check(crypto.createHash('sha256').update(bytes).digest('hex') === capture.sha256, 'Heap capture hash differs');
  const header = Buffer.from(state.bootstrapPlan.header, 'hex').subarray(0, 16);
  const arrays = new Set();
  for (let at = 0; at + 0x5a0 <= bytes.length; at += 8)
    if (bytes.subarray(at, at + 16).equals(header) && bytes[at + 0x10] === 0 &&
        !bytes.subarray(at + 0x18, at + 0x30).some(Boolean) &&
        unusedTail(bytes.subarray(at, at + 0x5a0))) arrays.add(capture.address + at);
  const matches = [];
  for (let at = 0; at + 0x78 <= bytes.length; at += 8) {
    const array = Number(bytes.readBigUInt64LE(at + 0x48));
    if (arrays.has(array) && bytes.readBigUInt64LE(at + 0x20) === BigInt(report.storageSize) &&
        bytes.readBigUInt64LE(at + 0x28) === 0x4000n && bytes.readBigUInt64LE(at + 0x50) === 0x5a0n &&
        bytes.readInt32LE(at + 0x58) === 10 && bytes.readBigUInt64LE(at + 0x60) === 14n)
      matches.push({array, owner: capture.address + at});
  }
  check(matches.length === 1, 'Expected exactly one owned cache array and owner');
  const proof = {session: boot.session, report: current.report, capture, ...matches[0], attempts: []};
  const save = () => fs.writeFile(file, JSON.stringify(proof, null, 2) + '\n', {mode: 0o600});
  await save();
  for (let attempt = 0; attempt < 6; attempt++) {
    // Probe the header within captured memory; install verifies the full array.
    const result = await memory.request(socket, {command: 'retained-bootstrap-move', address: proof.array,
      length: 0x40, anchorOffset: 0, anchorHeader: true});
    proof.attempts.push(result);
    await save();
    if (result.verified) break;
  }
  check(proof.attempts.at(-1)?.verified, 'Owned-array mapping was not established');
  await finish(proof);
}

if (require.main === module) main().catch(error => {console.error(error); process.exitCode = 1;});
