const fs = require('node:fs/promises');
const path = require('node:path');
const {a} = require('../shared/rpc.cjs');
const {run} = require('../shared/stages.cjs');
const {connect} = require('./tools/fs-memory.cjs');
const runtime = require('./ro/runtime.cjs');
const {read, save, local, hash} = runtime;
const check = (ok, message) => {if (!ok) throw Error(message);};

async function main() {
  const flags = process.argv.slice(2);
  check(flags.length <= 2 && flags.every(flag => ['--dump-firmware', '--hold-rpc'].includes(flag)) &&
    new Set(flags).size === flags.length, 'Usage: pipeline.cjs [--dump-firmware] [--hold-rpc]');
  const dumpFirmware = flags.includes('--dump-firmware');
  const holdRpc = flags.includes('--hold-rpc');
  check(!holdRpc || typeof process.send === 'function', 'Held FS mode requires runner IPC');
  const file = path.join(__dirname, 'pipeline.json');
  const record = {session: a.session, started: new Date().toISOString(), stages: []};
  await fs.writeFile(file, JSON.stringify(record) + '\n', {flag: 'wx', mode: 0o600});
  async function stage(name, action) {
    console.log(name);
    const row = {name, started: new Date().toISOString(), status: 'running'};
    record.stages.push(row); await save(file, record);
    try {
      row.result = await action(); row.status = 'complete';
      return row.result;
    } catch (error) {row.status = 'failed'; row.error = error.stack; throw error;}
    finally {row.finished = new Date().toISOString(); await save(file, record);}
  }
  const script = (folder, name, args = [], timeout) =>
    stage(folder + '/' + name, () => run(path.join(__dirname, folder), name, args, timeout));
  let acquisition;
  try {
    console.log('1/4 Checking the console');
    await script('app', 'map');
    await script('app', 'inspect-mounts');
    await stage('Remove exploit scratch from prior launches', () => require('./cleanup-scratch.cjs').main());
    const app = await script('app', 'qualify');
    check(app.verified && app.cleanedUp, 'Application worker qualification failed');
    console.log('2/4 Acquiring FS memory');
    await stage('FS read, write and restoration', async () => {
      acquisition = await require('./acquire.cjs').start();
      return {report: acquisition.report, proof: acquisition.proof};
    });
    console.log('3/4 Starting the native helper');
    const captured = await read(acquisition.report);
    await new Promise(resolve => setTimeout(resolve, 2000));
    if (!captured.retainedMemory?.captures?.some(c => c.length === 0x4d8000))
      await script('retained', 'find-metadata', [acquisition.supervisorReport]);
    await script('ro', 'install-retained');
    await stage('FS layout', () => require('./ro/layout.cjs').main());
    await stage('Owned FS root', () => require('./ro/objects.cjs').main('root'));
    await stage('Owned FS notifier', () => require('./ro/objects.cjs').main('notifier'));
    const layout = await read(local(a.session, '-layout.json'));
    await script('ro', 'test-sm', [String(layout.sdk + 0x12a0)]);
    await script('ro', 'copy-loader-root', [String(layout.target.entry)]);
    await script('ro', 'prepare-workers');
    const donor = await stage('RO payload and current console policy', async () => {
      const memory = await connect(), sm = await read(local(a.session, '-sm-proof.json'));
      const monitor = await memory.request({command: 'root-native-sm-service', handle: sm.export.handle, name: 'pm:dmnt'});
      check(!monitor.result && !monitor.transport, 'PM monitor did not open');
      const loader = await read(local(a.session, '-loader-root-copy.json'));
      const code = await memory.request({command: 'root-native-code-open', handle: loader.copy.handle, title: '0x0400000000000037'});
      check(!code.result, 'Native RO CodeFS did not open');
      for (const name of ['main.npdm', 'main'])
        await memory.request({command: 'root-native-file-capture', handle: code.handle, name});
      const result = await memory.request({command: 'root-native-donor-prepare'});
      check(result.ready && result.files.every(item => item.verified), 'The payload files did not verify');
      return result;
    });
    await script('ro', 'bind-sessions');
    await script('ro', 'prepare-event');
    const gate = await script('ro', 'test-gate');
    check(gate.verified && gate.restored, 'The native request gate did not qualify');
    const launch = await script('ro', 'launch', [], 300000);
    check(launch.imageAccepted && launch.restored && launch.pid, 'PM did not accept the payload');
    const activation = await stage('Start RO payload', () => runtime.activate(launch.file));
    const proof = await stage('RO instruction challenges', () => runtime.probe(activation.pid));
    const handoff = await stage('Restore Loader session', () => runtime.handoff());
    const payload = donor.files.find(item => item.name === 'main');
    check(payload.sha256 === hash(await fs.readFile(path.join(__dirname, 'ro/payload/main'))), 'Payload changed during launch');
    await save(runtime.executionFile, {verified: true, session: a.session, report: acquisition.report,
      pid: proof.pid, programId: '0400000000000037', payloadSha256: payload.sha256,
      proof: proof.file, launch: launch.file, identity: activation.file, handoff: handoff.file});
    console.log('4/4 Running the FS demo');
    await script('process', 'bind-notifier');
    const mapping = await script('process', 'qualify');
    check(mapping.verified && mapping.cleanup?.finished, 'Native process mapping did not qualify and clean up');
    const execution = await stage('FS instruction challenges', () => require('./process/fs-execute.cjs').main());
    check(execution.verified && execution.restored, 'FS execution or restoration did not verify');
    if (dumpFirmware) {
      console.log('Firmware: beginning read-only system-program CodeFS capture');
      record.firmwareDump = await stage('Enumerate and capture installed system code',
        () => require('./firmware-dump.cjs').main(execution.file));
    }
    const donorCleanup = await stage('Remove RO donor files', async () => {
      const memory = await connect();
      const donor = await memory.request({command: 'root-native-donor-cleanup'});
      check(donor.cleanedUp, 'Donor files did not clean up');
      return donor;
    });
    if (holdRpc) {
      const held = {...execution, cleanup: {deferred: true, donor: donorCleanup},
        retainedEndpoint: {ready: true, session: a.session, socketPath: acquisition.socketPath},
        started: record.started};
      await save(path.join(__dirname, 'fs-execution-current.json'), held);
      record.hold = {state: 'ready', started: new Date().toISOString()};
      await save(file, record);
      const release = require('./hold.cjs').waitForRelease();
      process.send({type: 'held-ready', session: a.session});
      const reason = await release;
      record.hold = {...record.hold, state: 'releasing', reason, released: new Date().toISOString()};
      await save(file, record);
    }
    const cleanup = await stage('Release FS resources', async () =>
      ({...await acquisition.retire(), donor: donorCleanup}));
    const result = {...execution, cleanup, started: record.started, completed: new Date().toISOString()};
    await save(path.join(__dirname, 'fs-execution-current.json'), result);
    record.verified = true; record.result = result;
  } catch (error) {
    record.error = error.stack;
    if (acquisition) {
      try {
        const latest = await read(acquisition.report);
        // Before promotion, the acquisition still owns its original restoration
        // path. Later failures may have native state that needs a console reboot.
        if (!latest.nativeRo && !latest.retainedMemory?.promoted)
          record.failureCleanup = await acquisition.retire();
        else await acquisition.abandon();
      } catch (cleanupError) {
        record.cleanupError = cleanupError.stack;
        await acquisition.abandon();
      }
    }
    throw error;
  } finally {record.finished = new Date().toISOString(); await save(file, record);}
}

module.exports = {main};
if (require.main === module) main().catch(error => {console.error(error.stack); process.exitCode = 1;});
