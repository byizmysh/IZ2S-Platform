const fs = require('node:fs/promises');
const path = require('node:path');
const {connect} = require("../tools/fs-memory.cjs");
const {exportHandle} = require("./export-handle.cjs");
const {Gate} = require("./gate.cjs");
const check = (ok, message) => { if (!ok) throw Error(message); };

async function main() {
  const memory = await connect();
  const proof = JSON.parse(await fs.readFile(path.join(__dirname, memory.info.session + '-loader-root-copy.json')));
  const bound = JSON.parse(await fs.readFile(path.join(__dirname, memory.info.session + '-bound-sessions.json')));
  check(proof.session === memory.info.session && bound.report === memory.reportFile, 'Stale session bindings');
  const row = bound.rows.find(r => r.domain === proof.ownDomain);
  const state = await memory.request({command: 'root-native-info'});
  const copy = await exportHandle(memory, row.receive, 'owned domain server readiness');
  const waiters = bound.waiters;
  check(Array.isArray(waiters) && waiters.length === 7, 'The current boot has no verified waiter array');
  const event = await fs.readFile(path.join(__dirname, memory.info.session + '-gate-event.json')).then(JSON.parse)
    .catch(error => { if (error.code !== 'ENOENT') throw error; return null; });
  check(!event || event.verified && event.report === memory.reportFile, 'Stale event proof');
  const gate = new Gate(memory, row, state.apiWorkers, waiters, copy.handle, event);
  const result = {session: memory.info.session, report: memory.reportFile, row, copy, event, steps: gate.steps};
  const file = path.join(__dirname, memory.info.session + '-gate-test-' + Date.now() + '.json');
  const save = () => fs.writeFile(file, JSON.stringify(result, null, 2) + '\n', {mode: 0o600});
  await save();
  try {
    result.worker = await memory.request({command: 'root-native-worker-prepare', kind: 'control-test', handle: state.inspectionDomain.handle});
    result.park = await gate.parkSession(); await save();
    console.log('parked', result.park);
    await memory.request({command: 'root-native-worker-release'});
    check(await gate.pending(), 'First worker request is not queued');
    result.before = await memory.request({command: 'root-native-worker-state'}); await save();
    check(result.before.values[1] === result.before.sentinel, 'First request ran through the park');
    await gate.step(); await save();
    result.afterFirst = await memory.request({command: 'root-native-worker-state'}); await save();
    console.log('after first', result.afterFirst);
    check(result.afterFirst.values[1] === '0' && result.afterFirst.values[2] === result.afterFirst.sentinel &&
      !result.afterFirst.finished && await gate.pending(), 'Gate did not release exactly one request');
    await gate.step(); await save();
    result.completion = await memory.request({command: 'root-native-worker-join'});
    check(result.completion.responses.every(r => r.result === 0), 'A stepped IPC response failed');
    result.verified = true;
  } catch (error) {
    result.error = error.stack;
    throw error;
  } finally {
    result.outstanding = gate.outstanding;
    await save();
    await gate.restore();
    result.restoration = gate.restoration;
    result.restored = true; await save();
  }
  result.closed = await memory.request({command: 'root-native-close-handle', handle: copy.handle});
  await save();
  console.log(JSON.stringify({file, verified: result.verified, restored: result.restored, attempts: gate.steps.map(s => s.attempts.length)}));
}
main().catch(e => { console.error(e.stack); process.exitCode = 1; });
