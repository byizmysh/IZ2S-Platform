const fs = require('node:fs/promises');
const path = require('node:path');
const {connect} = require("../tools/fs-memory.cjs");
const {exportHandle} = require("./export-handle.cjs");
const check = (ok, message) => { if (!ok) throw Error(message); };

async function main() {
  const memory = await connect();
  const ring = JSON.parse(await fs.readFile(path.join(__dirname, memory.info.session + '-thread-ring.json')));
  check(ring.session === memory.info.session, 'Stale thread ring');
  const groups = new Map();
  for (const node of ring.nodes.filter(t => t.state === 3 && t.size === 0xc000 && t.argument === 0)) {
    if (!groups.has(node.entry)) groups.set(node.entry, []);
    groups.get(node.entry).push(node);
  }
  const matches = [...groups.values()].filter(rows => rows.length === 7);
  check(matches.length === 1, 'Native API-worker group is not unique');
  const file = path.join(__dirname, memory.info.session + '-api-workers.json'), proof = {session: memory.info.session, workers: []};
  const save = () => fs.writeFile(file, JSON.stringify(proof, null, 2) + '\n', {mode: 0o600});
  for (const node of matches[0]) {
    const current = await memory.read(node.address, 0x1c0);
    check(current[0x40] === 3 && current.readUInt32LE(0x1b0) === node.handle &&
      current.readBigUInt64LE(0x68) === BigInt(node.entry), 'Native API thread changed');
    const copy = await exportHandle(memory, node.handle, 'FS API thread ' + node.id);
    const identity = await memory.request({command: 'root-native-thread-id', handle: copy.handle});
    check(identity.result === 0 && Number(identity.value) === node.id, 'Copied API thread has the wrong ID');
    proof.workers.push({address: node.address, sourceHandle: node.handle, id: node.id,
      entry: node.entry, handle: copy.handle, proof: copy.proof});
    await save();
  }
  await memory.request({command: 'root-native-worker-bind', workers: proof.workers});
  proof.cancellation = await memory.request({command: 'root-native-worker-cancel'});
  await save();
  console.log(JSON.stringify({file, ...proof}));
}

main().catch(error => {console.error(error); process.exitCode = 1;});
