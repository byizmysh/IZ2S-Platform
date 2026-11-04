const fs = require('node:fs/promises');
const path = require('node:path');
const {connect} = require("../tools/fs-memory.cjs");
const {exportHandle} = require("./export-handle.cjs");

async function main() {
  const memory = await connect();
  const address = Number(process.argv[2]);
  const name = 'pm:shell';
  const before = await memory.read(address - 4, 16);
  const handle = before.readUInt32LE(4);
  const initialized = before.readUInt32LE(8) === 0 && before.readUInt16LE(12) === 0x101 &&
    await memory.pointer(address - 8) === address;
  if (!initialized || handle < 0x8000 || handle >= 0x2000000)
    throw Error('Candidate does not have an initialized handle and guard');
  const proof = {session: memory.info.session, report: memory.reportFile, address,
    header: before.toString('hex'), sourceHandle: handle};
  const file = path.join(__dirname, memory.info.session + '-sm-' + address.toString(16) + '-' + name.replace(':', '-') + '.json');
  const save = () => fs.writeFile(file, JSON.stringify(proof, null, 2) + '\n', {mode: 0o600});
  await save();
  proof.export = await exportHandle(memory, handle, 'initialized SM candidate');
  await save();
  proof.service = await memory.request({command: 'root-native-sm-service', handle: proof.export.handle, name});
  await save();
  if (proof.service.transport || proof.service.result) {
    proof.closed = await memory.request({command: 'root-native-close-handle', handle: proof.export.handle});
    await save();
    throw Error('SM service lookup did not succeed: ' + JSON.stringify(proof.service));
  }
  const pid = Buffer.alloc(8); pid.writeBigUInt64LE(0x0400000000000037n);
  proof.program = await memory.request({command: 'root-native-ipc', handle: proof.service.moves[0], id: 12, hex: pid.toString('hex')});
  await save();
  if (proof.program.result) throw Error('PM identity query did not succeed');
  await fs.writeFile(path.join(__dirname, memory.info.session + '-sm-proof.json'), JSON.stringify(proof, null, 2) + '\n', {mode: 0o600});
  console.log(JSON.stringify({file, ...proof}));
}

main().catch(error => {console.error(error); process.exitCode = 1;});
