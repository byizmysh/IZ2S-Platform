'use strict';

// Standalone raw-FSP reader. Importing performs no RPC; no SDK FS mount/query wrappers.
const fs = require('node:fs/promises');
const path = require('node:path');
const crypto = require('node:crypto');
const {TextDecoder} = require('node:util');
const IDS = new Set(['0400000000000804', '040000000000080c']);
const TOTAL_CAP = 128 * 1024 * 1024;
const CHUNK_CAP = 1024 * 1024;
const active = new WeakSet(), poisoned = new WeakSet(), poisonedIds = new Set();
const check = (ok, message) => {if (!ok) throw Error(message);};
const hash = bytes => crypto.createHash('sha256').update(bytes).digest('hex');

async function exclusive(filename, bytes) {
  const output = await fs.open(filename, 'wx', 0o600);
  try {await output.writeFile(bytes); await output.sync();} finally {await output.close();}
}

function normalizeId(value) {
  check(typeof value === 'string' && /^(?:0x)?[0-9a-fA-F]{16}$/.test(value), 'Expected one fixed 16-digit SystemData ID');
  const id = value.replace(/^0x/, '').toLowerCase();
  check(IDS.has(id), 'Only installed Help0804 and EULA080c IDs are supported');
  return id;
}

function openInput(id) {
  const input = Buffer.alloc(16);
  input.writeBigUInt64LE(3n);
  input.writeBigUInt64LE(BigInt('0x' + normalizeId(id)), 8);
  return input;
}

// Parse only host bytes. It neither extracts files nor follows paths on the host.
function parseRomfs(bytes) {
  check(Buffer.isBuffer(bytes) && bytes.length >= 0x50 && bytes.length <= TOTAL_CAP, 'Invalid RomFS image size');
  const number64 = offset => {
    const value = bytes.readBigUInt64LE(offset);
    check(value <= BigInt(Number.MAX_SAFE_INTEGER), 'RomFS integer exceeds exact host range');
    return Number(value);
  };
  const header = {headerSize: number64(0), directoryHashOffset: number64(8), directoryHashSize: number64(16),
    directoryMetaOffset: number64(24), directoryMetaSize: number64(32), fileHashOffset: number64(40),
    fileHashSize: number64(48), fileMetaOffset: number64(56), fileMetaSize: number64(64), fileDataOffset: number64(72)};
  check(header.headerSize === 0x50, 'Unsupported RomFS header size');
  const ranges = [
    [header.directoryHashOffset, header.directoryHashSize, 'directory hash'],
    [header.directoryMetaOffset, header.directoryMetaSize, 'directory metadata'],
    [header.fileHashOffset, header.fileHashSize, 'file hash'],
    [header.fileMetaOffset, header.fileMetaSize, 'file metadata'],
  ];
  for (const [offset, size, name] of ranges)
    check(offset >= 0x50 && offset <= bytes.length && size <= bytes.length - offset, 'RomFS ' + name + ' exceeds image');
  check(header.directoryMetaSize >= 24 && header.directoryMetaSize + header.fileMetaSize <= 16 * 1024 * 1024,
    'RomFS metadata exceeds bounded parser capacity');
  check(header.fileDataOffset >= 0x50 && header.fileDataOffset <= bytes.length, 'Invalid RomFS file-data offset');
  const nonempty = ranges.filter(([, size]) => size > 0).sort((a, b) => a[0] - b[0]);
  for (let i = 0; i < nonempty.length; i++) {
    if (i) check(nonempty[i - 1][0] + nonempty[i - 1][1] <= nonempty[i][0], 'RomFS metadata regions overlap');
  }
  const dirs = bytes.subarray(header.directoryMetaOffset, header.directoryMetaOffset + header.directoryMetaSize);
  const files = bytes.subarray(header.fileMetaOffset, header.fileMetaOffset + header.fileMetaSize);
  const decoder = new TextDecoder('utf-8', {fatal: true});
  const nameAt = (table, at, fixed, allowEmpty) => {
    check(Number.isInteger(at) && at >= 0 && at % 4 === 0 && at <= table.length - fixed, 'Invalid RomFS metadata entry offset');
    const length = table.readUInt32LE(at + fixed - 4);
    check(length <= 0x1000 && length <= table.length - at - fixed, 'Invalid RomFS name length');
    const name = decoder.decode(table.subarray(at + fixed, at + fixed + length));
    check((allowEmpty || name.length > 0) && !name.includes('/') && !name.includes('\\') &&
      !name.includes('\0') && name !== '.' && name !== '..', 'Unsafe RomFS entry name');
    return name;
  };
  const END = 0xffffffff, queue = [{at: 0, parent: 0, prefix: '/', depth: 0}], seenDirs = new Set(), seenFiles = new Set();
  const directories = [], entries = [], names = new Set();
  const pathCharacterBudget = 16 * 1024 * 1024;
  let pathCharacters = 0;
  const registerPath = pathname => {
    check(pathname.length <= 0x4000 && !names.has(pathname), 'RomFS duplicate/overlong path');
    check(pathname.length <= pathCharacterBudget - pathCharacters, 'RomFS aggregate pathname budget exceeded');
    pathCharacters += pathname.length; names.add(pathname);
  };
  for (let q = 0; q < queue.length; q++) {
    const current = queue[q];
    check(queue.length <= 50000 && current.depth <= 128 && !seenDirs.has(current.at), 'RomFS directory cycle/count/depth limit');
    seenDirs.add(current.at);
    const name = nameAt(dirs, current.at, 24, current.at === 0);
    check(current.at !== 0 || name === '', 'RomFS root directory name must be empty');
    check(current.at === 0 || dirs.readUInt32LE(current.at) === current.parent, 'RomFS directory parent mismatch');
    const pathname = current.at === 0 ? '/' : current.prefix + name + '/';
    registerPath(pathname); directories.push({path: pathname, metadataOffset: current.at});
    let child = dirs.readUInt32LE(current.at + 8);
    const childSiblings = new Set();
    while (child !== END) {
      check(!childSiblings.has(child) && queue.length < 50000, 'RomFS directory sibling cycle/count limit');
      childSiblings.add(child);
      nameAt(dirs, child, 24, false);
      queue.push({at: child, parent: current.at, prefix: pathname, depth: current.depth + 1});
      child = dirs.readUInt32LE(child + 4);
    }
    let file = dirs.readUInt32LE(current.at + 12);
    while (file !== END) {
      check(!seenFiles.has(file) && entries.length < 100000, 'RomFS file cycle/count limit');
      seenFiles.add(file);
      const filename = nameAt(files, file, 32, false);
      check(files.readUInt32LE(file) === current.at, 'RomFS file parent mismatch');
      const relative = files.readBigUInt64LE(file + 8), size = files.readBigUInt64LE(file + 16);
      const absolute = BigInt(header.fileDataOffset) + relative;
      check(absolute <= BigInt(bytes.length) && size <= BigInt(bytes.length) - absolute, 'RomFS file extends outside image');
      if (size > 0n) {
        check(absolute >= BigInt(header.headerSize), 'RomFS file payload overlaps header');
        const end = absolute + size;
        for (const [metadataOffset, metadataSize] of nonempty)
          check(end <= BigInt(metadataOffset) || absolute >= BigInt(metadataOffset + metadataSize),
            'RomFS file payload overlaps metadata');
      }
      const full = pathname + filename;
      registerPath(full);
      entries.push({path: full, metadataOffset: file, offset: Number(absolute), size: Number(size),
        html: /\.(?:html?|xhtml)$/i.test(filename)});
      file = files.readUInt32LE(file + 4);
    }
  }
  return {format: 'RomFS', header, directories, files: entries, htmlFiles: entries.filter(entry => entry.html),
    pathCharacters, pathCharacterBudget,
    hashScope: 'Metadata enumeration only; whole image SHA256 is recorded by the capture receipt.'};
}

/** capture({s,live,report}, {session,dataId,outputDirectory?}, {filesystem})
 * root = report.ownedRoot.clones[0], already initialized/non-domain, serialized by caller.
 * filesystem must be the fresh controller module with readStorage/native verification.
 */
async function capture(context, options, deps) {
  const {s, live, report} = context || {}, filesystem = deps?.filesystem;
  check(s && live && report && options && typeof filesystem?.readStorage === 'function', 'Missing fresh controller context/helpers');
  check(live.platform === 2 && typeof live.session === 'string' && options.session === live.session,
    'Explicit current Switch2 session ID required');
  check(!poisoned.has(s) && !poisonedIds.has(live.session), 'Session has an uncertain RPC outcome; pair a fresh session');
  check(report.ownedRoot?.initialized && Array.isArray(report.ownedRoot.clones), 'Initialized owned FsProxy root missing');
  const rootHandle = report.ownedRoot.clones[0];
  check(s.handles instanceof Set && s.threads instanceof Set && !s.threads.size && !s.pendingIpc &&
    Number.isInteger(rootHandle) && rootHandle > 0 && s.handles.has(rootHandle), 'Owned root or idle runner unavailable');
  check(typeof s.ipc === 'function' && typeof s.close === 'function', 'Required ordinary Session methods missing');
  check(Number.isSafeInteger(s.base) && s.base > 0 && Number.isInteger(s.bytes) && s.bytes >= 0x8000,
    'Owned scratch allocation insufficient');
  const id = normalizeId(options.dataId), output = options.outputDirectory ?? path.join(__dirname, 'systemdata-readonly-captures');
  check(typeof output === 'string' && path.isAbsolute(output), 'Host output directory must be absolute');
  const chunkSize = Math.min(CHUNK_CAP, s.bytes - 0x6000, s.bulkDownloads ? 0x400000 : 0x40000);
  check(Number.isInteger(chunkSize) && chunkSize > 0, 'No owned storage-read buffer capacity');
  const sourceSha256 = hash(await fs.readFile(__filename));
  check(!active.has(s), 'A SystemData capture is already active');
  check(!poisoned.has(s) && !poisonedIds.has(live.session), 'Session became uncertain while preparing capture');
  active.add(s);
  const borrowed = new Set(s.handles), children = [];
  const receipt = {operation: 'READ_ONLY_SYSTEMDATA_RAW_FSP202', version: 1, session: live.session,
    started: new Date().toISOString(), completed: false, persistentConsoleWrites: false,
    handlerSha256: sourceSha256, rootHandle, dataId: '0x' + id, storageId: 3,
    totalCap: TOTAL_CAP, chunkCap: CHUNK_CAP, chunkSize, steps: [], chunks: [], cleanup: []};
  let safe = true, directory, outputFile, currentStep;
  const step = async (label, work) => {
    check(safe && !s.threads.size && !s.pendingIpc, 'Runner state changed or earlier RPC outcome uncertain');
    currentStep = label; safe = false;
    const value = await work(); safe = true;
    receipt.steps.push({label, at: new Date().toISOString()}); return value;
  };
  const track = reply => {
    for (const handle of [...(reply.moves || []), ...(reply.copies || [])]) {
      check(Number.isInteger(handle) && handle > 0 && !borrowed.has(handle) && !children.includes(handle) && s.handles.has(handle),
        'Returned child handle lacks unique new ownership');
      children.push(handle);
    }
  };
  try {
    await fs.mkdir(output, {recursive: true, mode: 0o700});
    directory = await fs.mkdtemp(path.join(output, id + '-' + live.session.replace(/[^a-zA-Z0-9_-]/g, '_') + '-'));
    receipt.directory = directory;
    const opened = await step('FsProxy202 OpenDataStorageByDataId storage3', () =>
      s.ipc('SystemData readonly: OpenDataStorageByDataId', rootHandle, 202, openInput(id)));
    track(opened); receipt.openResult = opened.result; receipt.openCanonicalResult = opened.canonicalResult;
    check(!opened.result && opened.moves?.length === 1 && !opened.copies?.length, 'SystemData storage open failed or unexpected response shape');
    const storage = opened.moves[0];
    const sizeReply = await step('IStorage4 GetSize', () => s.ipc('SystemData readonly: IStorage GetSize', storage, 4));
    track(sizeReply);
    check(!sizeReply.result && !sizeReply.moves?.length && !sizeReply.copies?.length &&
      Buffer.isBuffer(sizeReply.data) && sizeReply.data.length >= 8, 'IStorage GetSize failed or malformed');
    const sizeBig = sizeReply.data.readBigUInt64LE(); receipt.size = sizeBig.toString();
    check(sizeBig >= 0x50n && sizeBig <= BigInt(TOTAL_CAP), 'SystemData storage outside50..128MiB capture bound');
    const size = Number(sizeBig), digest = crypto.createHash('sha256');
    receipt.file = path.join(directory, 'romfs.bin');
    receipt.partialFile = receipt.file + '.partial';
    outputFile = await fs.open(receipt.partialFile, 'wx', 0o600);
    let captured = 0;
    while (captured < size) {
      const count = Math.min(chunkSize, size - captured), offset = captured;
      const bytes = await step('IStorage0 Read ' + offset + '+' + count + ' with native SHA verification', () =>
        filesystem.readStorage(s, storage, offset, count));
      check(Buffer.isBuffer(bytes) && bytes.length === count, 'Storage read did not return the full requested chunk');
      await outputFile.writeFile(bytes); digest.update(bytes);
      receipt.chunks.push({offset, size: count, sha256: hash(bytes), nativeSha256MatchedByHelper: true}); captured += count;
      receipt.captured = captured;
    }
    await outputFile.sync(); await outputFile.close(); outputFile = null;
    receipt.sha256 = digest.digest('hex'); receipt.nativeVerifiedBytes = captured;
    receipt.hashScope = 'Whole host SHA256; every captured byte belongs to a chunk whose native SHA256 matched host bytes. Not a separate whole-console digest.';
    const image = await fs.readFile(receipt.partialFile);
    check(image.length === size && hash(image) === receipt.sha256, 'Host backup hash/size verification failed');
    await fs.link(receipt.partialFile, receipt.file);
    await fs.unlink(receipt.partialFile);
    receipt.partialFile = null;
    try {
      receipt.romfs = parseRomfs(image);
      const metadataFile = path.join(directory, 'romfs-metadata.json');
      await exclusive(metadataFile, JSON.stringify(receipt.romfs, null, 2) + '\n');
      receipt.metadataFile = metadataFile;
    } catch (error) {receipt.romfsParseError = error.message;}
    receipt.captureComplete = true;
  } catch (error) {receipt.error = {message: error.message, atStep: currentStep ?? null};}
  finally {
    if (outputFile) {try {await outputFile.sync();} catch {} try {await outputFile.close();} catch {}}
    if (safe && !s.threads.size && !s.pendingIpc) for (const handle of [...children].reverse()) {
      if (!s.handles.has(handle)) {receipt.cleanup.push({handle, closed: false, reason: 'Ownership disappeared'}); continue;}
      try {await step('Close newly owned IStorage', () => s.close(handle)); receipt.cleanup.push({handle, closed: true});}
      catch (error) {receipt.cleanup.push({handle, closed: false, error: error.message}); receipt.error ||= {message: error.message, atStep: currentStep}; break;}
    }
    receipt.remoteOutcomeUncertain = !safe;
    if (!safe) {poisoned.add(s); poisonedIds.add(live.session); receipt.sessionPoisoned = true;}
    receipt.retainedNewHandles = [...s.handles].filter(handle => !borrowed.has(handle));
    receipt.completed = receipt.captureComplete === true && !receipt.error && !receipt.retainedNewHandles.length;
    receipt.finished = new Date().toISOString();
    try {
      if (directory) {receipt.receiptFile = path.join(directory, 'receipt.json'); await exclusive(receipt.receiptFile, JSON.stringify(receipt, null, 2) + '\n');}
    } finally {active.delete(s);}
  }
  return receipt;
}

module.exports = {capture, normalizeId, openInput, parseRomfs};
