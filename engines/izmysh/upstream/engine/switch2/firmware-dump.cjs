// Optional, read-only CodeFS capture after the verified FS demo and before
// the upstream runner retires its SmileBASIC-backed RPC. No raw NAND access.
const fs = require('node:fs/promises');
const {createReadStream} = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const {connect} = require('./tools/fs-memory.cjs');

const check = (ok, message) => {if (!ok) throw Error(message);};
const hash = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const read = file => fs.readFile(file, 'utf8').then(JSON.parse);
const MISSING_CODE_FS = 1032;

async function save(file, value) {
  const temporary = file + '.' + process.pid + '.pending';
  await fs.writeFile(temporary, JSON.stringify(value, null, 2) + '\n', {flag: 'wx', mode: 0o600});
  await fs.rename(temporary, file);
}

async function verifyCapture(entry) {
  check(entry?.size > 0 && entry.size <= 256 * 1024 * 1024 && entry.captured === entry.size &&
    typeof entry.file === 'string' && typeof entry.sha256 === 'string',
  'CodeFS capture is incomplete or exceeds its declared limit');
  let length = 0;
  const digest = crypto.createHash('sha256');
  for await (const chunk of createReadStream(entry.file, {highWaterMark: 1024 * 1024})) {
    length += chunk.length;
    check(length <= entry.size, 'CodeFS capture exceeds its recorded size');
    digest.update(chunk);
  }
  check(length === entry.size && digest.digest('hex') === entry.sha256,
    'CodeFS capture disk readback differs: ' + entry.file);
  return {name: entry.name, file: entry.file, size: entry.size, sha256: entry.sha256};
}

async function main(fsProofFile) {
  check(typeof fsProofFile === 'string', 'A verified FS execution proof file is required');
  const dumpDirectory = process.env.NS2_FIRMWARE_DUMP_DIR;
  check(dumpDirectory && path.isAbsolute(dumpDirectory), 'A dedicated firmware dump directory is required');
  const memory = await connect(), session = memory.info.session;
  const proof = await read(fsProofFile);
  check(proof.session === session && proof.report === memory.reportFile && proof.verified &&
    proof.trials?.length === 2 && proof.trials.every(row => row.verified && row.restored),
  'FS code has not been verified and restored in this run');
  const execution = await read(path.join(__dirname, 'ro/current-execution.json'));
  check(execution.session === session && execution.report === memory.reportFile && execution.verified,
    'The RO process identity is not verified for this run');
  const loader = await read(path.join(__dirname, 'ro', session + '-loader-root-copy.json'));
  check(loader.session === session && loader.report === memory.reportFile && loader.verified &&
    loader.restored && loader.copy?.handle, 'The retained CodeFS root is not verified');
  const initial = await memory.request({command: 'root-native-info'});
  const sm = initial.services?.find(item => item.name === 'pm:shell')?.sm;
  const binding = initial.codeFileSystems?.[0]?.handle;
  check(sm && binding && initial.domainCopies?.some(item => item.handle === loader.copy.handle) &&
    initial.worker?.joined, 'Required native service, CodeFS root, or idle worker is missing');

  const journalFile = path.join(dumpDirectory, 'firmware-dump.json');
  const journal = {session, report: memory.reportFile, fsProofFile,
    scope: 'installed full system-program CodeFS main and main.npdm only',
    started: new Date().toISOString(), entries: []};
  await fs.writeFile(journalFile, JSON.stringify(journal, null, 2) + '\n', {flag: 'wx', mode: 0o600});
  const lookup = await memory.request({command: 'root-native-sm-service', handle: sm, name: 'ncm'});
  check(!lookup.transport && !lookup.result && lookup.moves?.length === 1,
    'NCM service lookup failed');
  const ncmHandle = lookup.moves[0];
  const inventory = await memory.request({command: 'root-native-ncm-list-system', handle: ncmHandle,
    dumpOutput: true});
  const raw = await fs.readFile(inventory.rawFile);
  check(raw.length === 1024 * 16 && hash(raw) === inventory.sha256 && inventory.complete &&
    inventory.total === inventory.written && inventory.keys.length === inventory.total &&
    inventory.keys.length > 0, 'NCM inventory is truncated, empty, or failed local verification');
  journal.inventory = {total: inventory.total, rawFile: inventory.rawFile,
    sha256: inventory.sha256, keys: inventory.keys};
  await save(journalFile, journal);
  await memory.request({command: 'root-native-close-handle', handle: ncmHandle});
  console.log('Firmware: NCM listed ' + inventory.total + ' full system programs');

  for (const key of inventory.keys) {
    const title = key.id;
    const code = await memory.request({command: 'root-native-code-open',
      handle: loader.copy.handle, title, storage: 3, attributes: 1, fresh: true, dumpOutput: true});
    if (code.result) {
      check(code.canonicalResult === MISSING_CODE_FS,
        'Unexpected OpenCodeFileSystem result for ' + title + ': ' + code.canonicalResult);
      journal.entries.push({title, version: key.version, status: 'codefs-unavailable',
        result: code.canonicalResult});
      await save(journalFile, journal);
      console.log('Firmware: ' + title + ' has no accessible CodeFS');
      continue;
    }
    check(code.handle && code.title === BigInt(title).toString() &&
      code.directory === path.join(dumpDirectory, title.slice(2)),
    'CodeFS handle or destination differs from the requested title');
    const info = await fs.readFile(code.codeInfo.file);
    check(hash(info) === code.codeInfo.sha256, 'CodeFS metadata disk readback differs');
    const files = [], absentFiles = [];
    for (const name of ['main.npdm', 'main']) {
      let captured = code.files?.findLast(item => item.name === name && item.captured === item.size);
      if (!captured) {
        try {captured = await memory.request({command: 'root-native-file-capture', handle: code.handle, name});}
        catch (error) {
          const absent = /^Code file did not open: (\d+)$/.exec(error.message);
          if (!absent) throw error; // Timeout/uncertain result: never send another native request.
          absentFiles.push({name, result: Number(absent[1])});
          continue;
        }
      }
      check(captured.name === name && captured.file === path.join(code.directory, name),
        'CodeFS capture path differs from the expected output');
      files.push(await verifyCapture(captured));
    }
    journal.entries.push({title, version: key.version, status: files.length ? 'captured' : 'no-code-files',
      codeInfo: {file: code.codeInfo.file, sha256: code.codeInfo.sha256}, files, absentFiles});
    await save(journalFile, journal);
    console.log('Firmware: ' + title + ' captured ' + files.map(file => file.name).join(', '));
    if (code.handle !== binding) {
      const closed = await memory.request({command: 'root-native-code-close', handle: code.handle});
      check(closed.closed && closed.handle === code.handle, 'Analysis CodeFS handle did not close');
    }
  }
  journal.completed = new Date().toISOString();
  journal.complete = true;
  journal.counts = {listed: journal.inventory.total,
    captured: journal.entries.filter(item => item.status === 'captured').length,
    unavailable: journal.entries.filter(item => item.status === 'codefs-unavailable').length,
    withoutFiles: journal.entries.filter(item => item.status === 'no-code-files').length};
  await save(journalFile, journal);
  console.log('Firmware: dump complete: ' + JSON.stringify(journal.counts));
  return {file: journalFile, ...journal.counts, complete: true};
}

module.exports = {main, verifyCapture};
