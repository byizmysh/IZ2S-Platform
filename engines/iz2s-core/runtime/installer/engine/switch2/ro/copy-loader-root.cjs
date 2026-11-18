// Briefly expose a live service object through an otherwise unused owned slot.
// CopyFromCurrentDomain creates the additional reference; restore the slot
// before any further request or close on the inspection domain.
const fs = require('node:fs/promises');
const path = require('node:path');
const {connect} = require("../tools/fs-memory.cjs");
const check = (ok, message) => { if (!ok) throw Error(message); };

async function main() {
  const memory = await connect();
  const targetEntry = Number(process.argv[2]);
  const owned = await memory.request({command: 'root-native-domain-open'});
  const target = await memory.read(targetEntry, 0x30);
  const targetDomain = Number(target.readBigUInt64LE(0x18));
  const manager = await memory.pointer(targetDomain + 0x18);
  const table = await memory.pointer(manager + 0x18), count = await memory.u32(manager + 0x10);
  check(targetEntry >= table && targetEntry < table + count * 0x30 && (targetEntry - table) % 0x30 === 0,
    'Target entry is not in its domain manager');
  check(owned.root > 0 && owned.root <= count, 'Owned ID is outside the domain table');
  const ownEntry = table + (owned.root - 1) * 0x30;
  const original = await memory.read(ownEntry, 0x30);
  const ownDomain = Number(original.readBigUInt64LE(0x18));
  check(ownDomain !== targetDomain && await memory.pointer(ownDomain + 0x18) === manager &&
    await memory.pointer(ownDomain) === await memory.pointer(targetDomain), 'Owned domain identity differs');
  const ownObject = Number(original.readBigUInt64LE(0x20));
  const targetObject = Number(target.readBigUInt64LE(0x20));
  const ownBefore = await memory.read(ownObject, 0x40);
  const targetBefore = await memory.read(targetObject, 0x40);
  const rootProof = JSON.parse(await fs.readFile(path.join(__dirname, './root/current-root.json')));
  check(rootProof.report === memory.reportFile, 'The caller identity proof is stale');
  const rootEvidence = JSON.parse(await fs.readFile(rootProof.proof));
  const callerPid = BigInt(rootProof.callerPid || rootEvidence.fields.find(f => f.name === 'root').before);
  check(callerPid > 1n && ownBefore.readBigUInt64LE(0x30) === callerPid && ownBefore.readUInt32LE(0x38) === 1,
    'Expected a fresh caller-owned fsp-srv root');
  check(targetBefore.readBigUInt64LE(0x30) === 1n && targetBefore.readUInt32LE(0x38) === 1 &&
    targetBefore.readBigUInt64LE(8) === ownBefore.readBigUInt64LE(8) &&
    targetBefore.readBigUInt64LE(0) - ownBefore.readBigUInt64LE(0) === 0x1ea0n &&
    target.readBigUInt64LE(0x28) - original.readBigUInt64LE(0x28) === 0xd0n,
  'Target differs from the native Loader-root layout');
  const proof = {session: memory.info.session, report: memory.reportFile, owned, ownEntry, ownDomain, ownObject,
    targetEntry, targetDomain, targetObject, manager, table, count,
    original: original.toString('hex'), target: target.toString('hex'),
    ownBefore: ownBefore.toString('hex'), targetBefore: targetBefore.toString('hex'), restored: false};
  const file = path.join(__dirname, memory.info.session + '-loader-root-copy.json');
  const save = () => fs.writeFile(file, JSON.stringify(proof, null, 2) + '\n', {mode: 0o600});
  await save();

  const baseline = await memory.request({command: 'root-native-domain-copy', handle: owned.handle, object: owned.root});
  try { check(await memory.u32(ownObject + 0x38) === 2, 'Owned slot failed its reference challenge'); }
  finally { await memory.request({command: 'root-native-close-handle', handle: baseline.handle}); }
  check(await memory.u32(ownObject + 0x38) === 1, 'Owned reference challenge did not restore');

  const oldPair = original.subarray(0x20), borrowedPair = target.subarray(0x20);
  await memory.write(ownEntry + 0x20, oldPair, borrowedPair);
  try {
    proof.copy = await memory.request({command: 'root-native-domain-copy', handle: owned.handle, object: owned.root});
    await save();
  } finally {
    await memory.write(ownEntry + 0x20, borrowedPair, oldPair);
    proof.restored = true;
    await save();
  }
  check((await memory.read(ownEntry + 0x18, 0x18)).equals(original.subarray(0x18)), 'Owned slot did not restore');
  check((await memory.read(targetEntry + 0x18, 0x18)).equals(target.subarray(0x18)), 'Original Loader slot changed');
  proof.ownReferences = await memory.u32(ownObject + 0x38);
  proof.targetReferences = await memory.u32(targetObject + 0x38);
  check(proof.ownReferences === 1 && proof.targetReferences === 2, 'Unexpected reference counts after copy');
  proof.verified = true;
  await save();
  console.log(JSON.stringify({file, ...proof}));
}

main().catch(error => {console.error(error); process.exitCode = 1;});
