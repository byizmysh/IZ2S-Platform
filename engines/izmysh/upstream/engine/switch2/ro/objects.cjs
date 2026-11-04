// Domain IDs locate our objects directly; clone challenges establish identity.
const fs = require('node:fs/promises');
const path = require('node:path');
const {connect} = require("../tools/fs-memory.cjs");
const {identityFields, promote} = require("../tools/fs-root-pid.cjs");
const {directory, check, ptr, save} = require("./layout.cjs");

async function locate(memory, layout, handle, inspect) {
  const locator = await memory.request({command: 'root-native-locator-open', handle});
  try {
    check(Number.isInteger(locator.root) && locator.root > 0 && locator.root <= layout.count,
      'Locator domain ID is outside the verified table');
    const address = layout.table + (locator.root - 1) * 0x30;
    const entry = await memory.read(address, 0x30), owner = ptr(entry, 0x18), object = ptr(entry, 0x20);
    check(owner >= layout.range.address && owner + 0x20 <= layout.range.address + layout.range.length,
      'Locator domain is outside the captured heap');
    const domain = await memory.read(owner, 0x20);
    check(ptr(domain) === layout.rootVptr + 0x28e0 && ptr(domain, 8) === layout.rootVptr + 0x2938 &&
      ptr(domain, 0x18) === layout.manager, 'Locator domain does not use the verified manager');
    return await inspect({locator, address, entry, owner, object});
  } finally {
    await memory.request({command: 'root-native-close-handle', handle: locator.handle});
  }
}

async function rootObject(memory, layout) {
  const owned = await memory.request({command: 'root-open'});
  if (!owned.initialized) await memory.request({command: 'root-initialize'});
  const count = owned.clones.length;
  const proofDirectory = path.join(directory, './root');
  const file = path.join(proofDirectory, memory.info.session + '-root-' + Date.now() + '.json');
  const evidence = {session: memory.info.session, report: memory.reportFile, rootHandle: owned.clones[0],
    started: new Date().toISOString(), discovery: 'owned domain ID', apply: true,
    corruptionCountBefore: memory.info.corruptionCount};
  const persist = () => fs.writeFile(file, JSON.stringify(evidence, null, 2) + '\n', {mode: 0o600});
  await fs.mkdir(proofDirectory, {recursive: true}); await persist();
  try {
    await locate(memory, layout, owned.clones[0], async location => {
      const before = await memory.read(location.object, 0x40);
      check(ptr(before) === layout.rootVptr && before.readUInt32LE(0x38) === count + 1 &&
        before.readBigUInt64LE(0x30) > 1n && before.readBigUInt64LE(0x30) < 0x1000000n,
      'Owned root identity or reference count differs');
      evidence.callerPid = before.readBigUInt64LE(0x30).toString();
      const challengedCount = count === 7 ? 5 : 7;
      await memory.request({command: 'root-clones', count: challengedCount});
      try {
        const after = await memory.read(location.object, 0x40), expected = Buffer.from(before);
        expected.writeUInt32LE(challengedCount + 1, 0x38);
        check(after.equals(expected), 'Root clone challenge changed unexpected bytes');
        evidence.challenge = {before: before.toString('hex'), after: after.toString('hex')};
      } finally { await memory.request({command: 'root-clones', count}); }
      check((await memory.read(location.object, 0x40)).equals(before), 'Root clone count did not restore');
      evidence.rootAddress = location.object;
      evidence.location = {...location, entry: location.entry.toString('hex')};
    });
    const before = await memory.read(evidence.rootAddress, 0x40);
    check(before.readUInt32LE(0x38) === count, 'Temporary locator reference was not released');
    evidence.matches = [{address: evidence.rootAddress, hex: before.toString('hex')}];
    evidence.cloneIdentityVerified = true;
    evidence.fields = await identityFields(memory, evidence.rootAddress); await persist();
    await promote(memory, evidence, persist);
    const info = await memory.request({command: 'retained-info'});
    check(info.ready && info.corruptionCount === evidence.corruptionCountBefore, 'Root promotion changed the memory endpoint');
    evidence.success = true;
    await fs.writeFile(path.join(proofDirectory, 'current-root.json'), JSON.stringify({
      session: memory.info.session, report: memory.reportFile, proof: file, rootHandle: evidence.rootHandle,
      rootAddress: evidence.rootAddress, callerPid: evidence.callerPid, pid: 1,
      fields: evidence.fields.map(({name, address}) => ({name, address})), bisAccessVerified: false}, null, 2) + '\n', {mode: 0o600});
    return {file, rootAddress: evidence.rootAddress, callerPid: evidence.callerPid};
  } catch (error) { evidence.error = error.stack; throw error; }
  finally { evidence.finished = new Date().toISOString(); await persist(); }
}

async function notifierObject(memory, layout) {
  const state = await memory.request({command: 'root-native-notifier-open'});
  check(state.notifiers.length === 1, 'Expected one owned notifier before location');
  const proof = {session: memory.info.session, report: memory.reportFile, discovery: 'owned domain ID'};
  await locate(memory, layout, state.notifiers[0], async location => {
    const before = await memory.read(location.object, 0x18);
    check(ptr(before) === layout.rootVptr - 0x16c0 && before.readUInt32LE(0x10) === 2,
      'Owned notifier type or reference count differs');
    await memory.request({command: 'root-native-notifier-clones', count: 7});
    try {
      const after = await memory.read(location.object, 0x18), expected = Buffer.from(before);
      expected.writeUInt32LE(8, 0x10);
      check(after.equals(expected), 'Notifier clone challenge changed unexpected bytes');
      proof.challenge = {before: before.toString('hex'), after: after.toString('hex')};
    } finally { await memory.request({command: 'root-native-notifier-clones', count: 1}); }
    check((await memory.read(location.object, 0x18)).equals(before), 'Notifier clone count did not restore');
    proof.E = location.object; proof.B = ptr(before, 8);
  });
  check(await memory.u32(proof.E + 0x10) === 1, 'Notifier locator reference was not released');
  const backend = await memory.read(proof.B, 0x48);
  check(ptr(backend, 0x10) === proof.B + 0x10 && ptr(backend, 0x18) === proof.B + 0x10,
    'Notifier event-list identity differs');
  proof.handleField = proof.B + 0x24; proof.handle = backend.readUInt32LE(0x24);
  proof.event = await memory.request({command: 'root-native-notifier-event'});
  proof.backend = backend.toString('hex'); proof.identityVerified = true;
  await save(memory.info.session + '-notifier-proof.json', proof);
  return proof;
}

async function main(kind) {
  check(['root', 'notifier'].includes(kind), 'Choose root or notifier');
  const memory = await connect();
  const layout = JSON.parse(await fs.readFile(path.join(directory, memory.info.session + '-layout.json')));
  check(layout.session === memory.info.session && layout.report === memory.reportFile, 'Stale native layout');
  return kind === 'root' ? rootObject(memory, layout) : notifierObject(memory, layout);
}
module.exports = {main, locate};
if (require.main === module) main(process.argv[2]).then(console.log).catch(error => { console.error(error.stack); process.exitCode = 1; });
