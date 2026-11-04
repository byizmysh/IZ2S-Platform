// Gate the real PM/Loader path and exchange two existing filesystem references.
const fs = require('node:fs/promises');
const path = require('node:path');
const {connect, encodeU64} = require("../tools/fs-memory.cjs");
const {exportHandle} = require("./export-handle.cjs");
const {Gate} = require("./gate.cjs");
const wire = require("../../shared/domain.cjs");
const check = (ok, message) => { if (!ok) throw Error(message); };
const ptr = (b, o = 0) => Number(b.readBigUInt64LE(o));

async function entries(memory, layout, domain) {
  const sentinel = domain + 0x20, rows = [], seen = new Set();
  let at = await memory.pointer(sentinel);
  while (at !== sentinel) {
    check(at >= layout.table && at < layout.table + layout.count * 0x30 &&
      (at - layout.table) % 0x30 === 0 && !seen.has(at) && rows.length < 128,
    'Domain list left its verified entry table or repeated an entry');
    seen.add(at);
    const b = await memory.read(at, 0x30);
    check(ptr(b, 0x18) === domain, 'Domain entry owner changed');
    rows.push({id: (at - layout.table) / 0x30 + 1, entry: at, domain,
      object: ptr(b, 0x20), metadata: ptr(b, 0x28), raw: b.toString('hex')});
    at = ptr(b);
  }
  return rows;
}

function requestInfo(hex) {
  const b = Buffer.from(hex, 'hex'), at = wire.dataStart(b), type = b.readUInt32LE() & 0xffff;
  check(at + 16 <= b.length, 'Saved request descriptors exceed the capture');
  if (type === 5 || type === 7) return {kind: 'control', command: b.readUInt32LE(at + 8)};
  check(type === 4 || type === 6, 'Unexpected Loader request type');
  const id = b.readUInt32LE(at + 4);
  if (b[at] === 2) return {kind: 'close', object: id};
  check(b[at] === 1 && b.toString('ascii', at + 16, at + 20) === 'SFCI', 'Saved domain request is malformed');
  return {kind: 'request', object: id, command: b.readUInt32LE(at + 24),
    input: b.subarray(at + 32, at + 16 + b.readUInt16LE(at + 2)).toString('hex')};
}

async function main() {
  const memory = await connect();
  const read = suffix => fs.readFile(path.join(__dirname, memory.info.session + suffix)).then(JSON.parse);
  const layout = await read('-layout.json'), root = await read('-loader-root-copy.json');
  const bound = await read('-bound-sessions.json'), types = await read('-filesystem-types.json');
  const event = await read('-gate-event.json');
  check([layout, root, bound, types, event].every(p => p.report === memory.reportFile) &&
    root.verified && types.compatible && event.verified, 'Current native proofs are incomplete');
  const wantedMount = 2;
  check(wantedMount === 1 || wantedMount === 2, 'Only the observed first or second target mount is supported');

  const state = await memory.request({command: 'root-native-info'});
  check(state.donor?.ready && (!state.worker || state.worker.joined), 'Donor or worker is not ready');
  const target = '0x0400000000000037';
  check(['0x0400000000000037', '0x0400000000000019', '0x040000000000000a', '0x040000000000004f',
    '0x0400000000000014', '0x0400000000000013', '0x040000000000001d'].includes(target), 'Unsupported native research target');
  check(target === (state.donor.title || '0x0400000000000037'), 'Donor policy belongs to a different target');
  const shell = state.services.find(x => x.name === 'pm:shell').handle;
  const row = bound.rows.find(r => r.domain === root.targetDomain);
  const donor = types.rows.find(r => r.name === 'donor');
  const server = await exportHandle(memory, row.receive, 'Loader server readiness');
  const gate = new Gate(memory, row, state.apiWorkers, bound.waiters, server.handle, event);
  const proof = {session: memory.info.session, report: memory.reportFile, target,
    wantedMount, flags: 0x10, row, donor, server, requests: [], steps: gate.steps, mounts: 0, swapped: false};
  const file = path.join(__dirname, memory.info.session + '-launch-' + Date.now() + '.json');
  const save = () => fs.writeFile(file, JSON.stringify(proof, null, 2) + '\n', {mode: 0o600});
  await save();
  console.log(JSON.stringify({stage: 'launch-prepared', file, wantedMount}));
  try {
    proof.worker = await memory.request({command: 'root-native-worker-prepare', kind: 'pm-launch',
      handle: shell, title: proof.target, flags: proof.flags});
    await gate.parkSession(); proof.parked = true; await save();
    await memory.request({command: 'root-native-worker-release'});
    const deadline = Date.now() + 240000;
    for (let i = 0; i < 160; i++) {
      while (!await gate.pending()) {
        const worker = await memory.request({command: 'root-native-worker-state'});
        if (worker.finished) break;
        check(Date.now() < deadline, 'PM launch did not reach the parked FS session');
        await new Promise(resolve => setTimeout(resolve, 25));
      }
      if ((await memory.request({command: 'root-native-worker-state'})).finished) break;
      const before = await entries(memory, layout, root.targetDomain);
      const step = await gate.step(), info = requestInfo(step.request);
      const after = await entries(memory, layout, root.targetDomain);
      const added = after.filter(a => !before.some(b => b.id === a.id && b.object === a.object));
      const record = {index: i, ...info, added, removed: before.filter(b => !after.some(a => a.id === b.id && a.object === b.object))};
      proof.requests.push(record);
      const rootId = (root.targetEntry - layout.table) / 0x30 + 1;
      if (info.kind === 'request' && info.object === rootId && info.command === 0) {
        const input = Buffer.from(info.input, 'hex');
        check(input.length >= 16 && input[0] === 1 && input[1] === 3 &&
          input.readBigUInt64LE(8) === BigInt(proof.target), 'The queued CodeFS mount is not the selected native target');
        proof.mounts++;
        record.targetMount = proof.mounts;
        check(added.length === 1 && added[0].metadata === donor.metadata, 'Target mount did not return one compatible filesystem');
        if (proof.mounts === wantedMount) {
          const code = added[0], currentDonor = await memory.read(donor.entry, 0x30);
          check(currentDonor.subarray(0x18).equals(Buffer.from(donor.raw, 'hex').subarray(0x18)) &&
            await memory.pointer(code.object) === donor.vptr && await memory.pointer(donor.object) === donor.vptr,
          'Filesystem identity or concrete adapter type changed');
          proof.exchange = {code, donor, started: new Date().toISOString(), firstWritten: false, secondWritten: false};
          await save();
          await memory.writeU64(code.entry + 0x20, code.object, donor.object);
          proof.exchange.firstWritten = true; await save();
          try {
            await memory.writeU64(donor.entry + 0x20, donor.object, code.object);
            proof.exchange.secondWritten = true; await save();
          } catch (error) {
            check(await memory.pointer(donor.entry + 0x20) === donor.object, 'Donor write failed without restoration');
            await memory.writeU64(code.entry + 0x20, donor.object, code.object);
            proof.exchange.rolledBack = true; await save();
            throw error;
          }
          check(await memory.pointer(code.entry + 0x18) === root.targetDomain &&
            await memory.pointer(code.entry + 0x20) === donor.object &&
            await memory.pointer(donor.entry + 0x18) === donor.domain &&
            await memory.pointer(donor.entry + 0x20) === code.object, 'Owning-reference exchange did not verify');
          proof.swapped = true; await save();
          console.log(JSON.stringify({stage: 'filesystem-exchanged', file, mount: proof.mounts, object: code.id}));
          break;
        }
      }
      await save();
      console.log(JSON.stringify({stage: 'stepped', index: i, kind: info.kind, object: info.object,
        command: info.command, added: added.map(a => a.id), mounts: proof.mounts}));
    }
  } catch (error) {
    proof.error = error.stack; await save();
    throw error;
  } finally {
    proof.outstanding = gate.outstanding; await save();
    await gate.restore();
    proof.restored = true; proof.restoration = gate.restoration; await save();
  }
  const until = Date.now() + 20000;
  while (!(await memory.request({command: 'root-native-worker-state'})).finished) {
    check(Date.now() < until, 'PM did not complete after the gate was restored');
    await new Promise(resolve => setTimeout(resolve, 50));
  }
  proof.completion = await memory.request({command: 'root-native-worker-join'});
  const reply = proof.completion.responses[0];
  proof.result = reply.result;
  if (!reply.result) proof.pid = Buffer.from(reply.data, 'hex').readBigUInt64LE().toString();
  proof.imageAccepted = proof.swapped && reply.result === 0;
  await save();
  await memory.request({command: 'root-native-close-handle', handle: server.handle});
  if (!proof.swapped && !reply.result) {
    proof.unmodifiedProcessCleanup = await memory.request({command: 'root-native-ipc', handle: shell,
      id: 1, hex: encodeU64(BigInt(proof.pid)).toString('hex')});
    await save();
  }
  console.log(JSON.stringify({stage: 'launch-complete', file, mounts: proof.mounts, result: proof.result,
    pid: proof.pid, imageAccepted: proof.imageAccepted, restored: proof.restored}));
}
if (require.main === module) main().catch(e => { console.error(e.stack); process.exitCode = 1; });

module.exports = {requestInfo, entries};
