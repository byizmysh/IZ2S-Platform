'use strict';

// Standalone controller extension candidate. Importing this file performs no RPC.
// Dependencies must come from the freshly paired controller, never an old session.
const fs = require('node:fs/promises');
const path = require('node:path');
const crypto = require('node:crypto');

const SAVE_ID = 0x8000000000000049n;
const ENTRY_SIZE = 0x1000;
const NS_INFO_BYTES = 0x20;
const NS_INFO_COMMAND = 3100;
const activeSessions = new WeakSet();
const poisonedSessions = new WeakSet();
const poisonedSessionIds = new Set();
const sha256 = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const check = (condition, message) => { if (!condition) throw Error(message); };

async function writeExclusive(filename, bytes) {
  const output = await fs.open(filename, 'wx', 0o600);
  try {
    await output.writeFile(bytes);
    await output.sync();
  } finally {await output.close();}
}

function save49Input() {
  const input = Buffer.alloc(0x48);
  input.writeBigUInt64LE(SAVE_ID, 0x20);
  return input;
}

function decodeEntry(bytes, firmware) {
  check(Buffer.isBuffer(bytes) && bytes.length === ENTRY_SIZE, 'Expected exactly4096 entry bytes');
  const storedFirmware = [...bytes.subarray(0x20, 0x23)];
  const history = bytes.subarray(0x10, 0x30);
  return {
    size: bytes.length,
    sha256: sha256(bytes),
    oldSafeVersionHex: bytes.subarray(0, 0x10).toString('hex'),
    historyHex: history.toString('hex'),
    historyAllZero: history.every(byte => byte === 0),
    state: bytes[0x10],
    knownState: bytes[0x10] <= 3,
    latestAttemptPosixSeconds: bytes.readBigInt64LE(0x18).toString(),
    firmware: storedFirmware,
    firmwareMatch: firmware === undefined ? null :
      storedFirmware.every((byte, index) => byte === firmware[index]),
    successPresentByte: bytes[0x23],
    successPresentBit0: !!(bytes[0x23] & 1),
    lastSuccessPosixSeconds: bytes.readBigInt64LE(0x28).toString(),
    padding1Hex: bytes.subarray(0x11, 0x18).toString('hex'),
    padding2Hex: bytes.subarray(0x24, 0x28).toString('hex'),
    uninterpretedTailSha256: sha256(bytes.subarray(0x30)),
  };
}

function decodeInfo(bytes) {
  check(Buffer.isBuffer(bytes) && bytes.length >= NS_INFO_BYTES, 'Short ns:vm3100 response');
  const info = bytes.subarray(0, NS_INFO_BYTES);
  return {
    hex: info.toString('hex'),
    state: info[0],
    elapsedAttemptNanoseconds: info.readBigInt64LE(8).toString(),
    elapsedSuccessNanoseconds: info.readBigInt64LE(0x10).toString(),
    reservedHex: Buffer.concat([info.subarray(1, 8), info.subarray(0x18, 0x20)]).toString('hex'),
    reservedAllZero: info.subarray(1, 8).every(byte => byte === 0) &&
      info.subarray(0x18, 0x20).every(byte => byte === 0),
  };
}

function anchorAnalysis(entry, before, after) {
  if (!before?.available || !after?.available) return {available: false, reason: '3100 not available on both sides'};
  const a = BigInt(entry.latestAttemptPosixSeconds), success = BigInt(entry.lastSuccessPosixSeconds);
  const one = before.info, two = after.info;
  const attempt1 = BigInt(one.elapsedAttemptNanoseconds), attempt2 = BigInt(two.elapsedAttemptNanoseconds);
  const success1 = BigInt(one.elapsedSuccessNanoseconds), success2 = BigInt(two.elapsedSuccessNanoseconds);
  const hasSuccess = entry.successPresentBit0 && [1, 3].includes(entry.state) && success1 > 0n && success2 > 0n;
  const stateMatches = one.state === two.state && one.state === entry.state;
  const elapsedNondecreasing = attempt2 >= attempt1 && success2 >= success1;
  const elapsedDifferenceStable = success2 - attempt2 === success1 - attempt1;
  const attemptNow1 = a * 1000000000n + attempt1;
  const attemptNow2 = a * 1000000000n + attempt2;
  const successNow1 = success * 1000000000n + success1;
  const successNow2 = success * 1000000000n + success2;
  const disagreement = attemptNow2 - successNow2;
  const absoluteDisagreement = disagreement < 0n ? -disagreement : disagreement;
  return {
    available: true,
    stateMatches,
    elapsedNondecreasing,
    elapsedDifferenceStable,
    hasSuccessAnchor: hasSuccess,
    candidateNowFromAttemptBeforeNanoseconds: attemptNow1.toString(),
    candidateNowFromAttemptAfterNanoseconds: attemptNow2.toString(),
    candidateNowFromSuccessBeforeNanoseconds: hasSuccess ? successNow1.toString() : null,
    candidateNowFromSuccessAfterNanoseconds: hasSuccess ? successNow2.toString() : null,
    attemptMinusSuccessAnchorNanoseconds: hasSuccess ? disagreement.toString() : null,
    anchorsAgreeWithinOneSecond: hasSuccess ? absoluteDisagreement <= 1000000000n : null,
    anchorUsableForFurtherReview: stateMatches && elapsedNondecreasing && elapsedDifferenceStable &&
      hasSuccess && absoluteDisagreement <= 1000000000n && one.reservedAllZero && two.reservedAllZero,
    caveat: 'Attempt cache can be stale after same-state failure; compare direct network time. No replacement bytes generated.',
  };
}

/**
 * Capture ONLY read-only SystemSave49:/entry using a fresh normal FsProxy session.
 *
 * context: {s, live, save?}; s is the existing owned/serialized Session.
 * options: {session, rootHandle, rootMode:'non-domain', rootInitialized:true,
 *           nsVmHandle?, timeStaticHandle?, firmware?:[major,minor,micro], outputDirectory?}
 * deps: {filesystem: require('../app/filesystem.cjs')}; use fresh-session module.
 *
 * Supplied handles stay owned by their caller. Newly returned handles are tracked
 * and closed after confirmed completed operations. An uncertain RPC aborts all
 * further RPC, including cleanup, and reports the retained handles for recovery.
 */
async function capture(context, options, deps) {
  const {s, live} = context || {};
  const filesystem = deps?.filesystem;
  check(s && live && options && filesystem, 'Expected context, options and fresh filesystem helpers');
  check(live.platform === 2 && typeof live.session === 'string' && live.session.length > 0 &&
    options.session === live.session, 'Explicit fresh session identity does not match');
  check(!poisonedSessions.has(s) && !poisonedSessionIds.has(live.session),
    'This session previously had an uncertain RPC outcome; use a freshly paired session');
  check(options.rootMode === 'non-domain' && options.rootInitialized === true,
    'Caller must supply an initialized, non-domain FsProxy session');
  check(s.handles instanceof Set && s.threads instanceof Set && !s.threads.size && !s.pendingIpc,
    'Capture requires an idle owned Session without pending IPC');
  check(Number.isSafeInteger(s.base) && s.base > 0 && Number.isInteger(s.bytes) && s.bytes >= 0x8000,
    'Owned scratch allocation is too small');
  for (const method of ['ipc', 'close', 'record']) check(typeof s[method] === 'function', 'Missing Session.' + method);
  for (const method of ['openFile', 'fileSize', 'readFile'])
    check(typeof filesystem[method] === 'function', 'Missing filesystem.' + method);
  const owned = (handle, role) => check(Number.isInteger(handle) && handle > 0 && handle < 0xffff8000 &&
    s.handles.has(handle), role + ' must be a current owned session handle');
  owned(options.rootHandle, 'FsProxy root');
  if (options.nsVmHandle !== undefined) owned(options.nsVmHandle, 'ns:vm');
  if (options.timeStaticHandle !== undefined) owned(options.timeStaticHandle, 'time IStaticService');
  check(new Set([options.rootHandle, options.nsVmHandle, options.timeStaticHandle].filter(x => x !== undefined)).size ===
    [options.rootHandle, options.nsVmHandle, options.timeStaticHandle].filter(x => x !== undefined).length,
    'Input service handles must be distinct');
  if (options.firmware !== undefined) check(Array.isArray(options.firmware) && options.firmware.length === 3 &&
    options.firmware.every(byte => Number.isInteger(byte) && byte >= 0 && byte <= 255), 'Invalid firmware bytes');
  const outputDirectory = options.outputDirectory === undefined ?
    path.join(__dirname, 'cache-readonly-captures') : options.outputDirectory;
  check(typeof outputDirectory === 'string' && path.isAbsolute(outputDirectory), 'Local output directory must be absolute');
  check(!activeSessions.has(s), 'A cache capture is already active for this Session');
  const handlerSha256 = sha256(await fs.readFile(__filename));
  check(!activeSessions.has(s), 'A cache capture became active for this Session');
  activeSessions.add(s);

  const started = new Date().toISOString();
  const receipt = {
    operation: 'READ_ONLY_SYSTEM_SAVE49_ENTRY', version: 1, session: live.session,
    started, completed: false, writeAuthorized: false, consolePersistentWrites: false,
    handlerSha256,
    platform: live.platform, sdkBase: live.sdk ?? null,
    source: {rootHandle: options.rootHandle, rootMode: options.rootMode,
      space: 0, systemSaveDataId: '0x8000000000000049', path: '/entry', expectedSize: ENTRY_SIZE,
      openCommand: 53, openInputHex: save49Input().toString('hex')},
    optionalServices: {nsVmHandle: options.nsVmHandle ?? null, timeStaticHandle: options.timeStaticHandle ?? null},
    steps: [], reads: [], cleanup: [],
  };
  let directory, cleanupSafe = true, lastStep;
  const callerHandles = new Set(s.handles), children = [];
  const step = async (label, work) => {
    check(cleanupSafe, 'An earlier RPC outcome is uncertain; no further RPC permitted');
    check(!s.threads.size && !s.pendingIpc, 'Runner acquired work during capture');
    lastStep = label;
    cleanupSafe = false;
    const value = await work();
    cleanupSafe = true;
    receipt.steps.push({label, completed: true, at: new Date().toISOString()});
    return value;
  };
  const trackReplyHandles = (reply, role) => {
    const handles = [...(reply?.moves || []), ...(reply?.copies || [])];
    for (const handle of handles) {
      check(Number.isInteger(handle) && handle > 0 && !callerHandles.has(handle) && !children.some(x => x.handle === handle),
        'Returned handle collides with an existing owned handle');
      check(s.handles.has(handle), 'Session did not register returned handle ownership');
      children.push({handle, role});
    }
  };
  const takeHandle = (reply, role) => {
    trackReplyHandles(reply, role);
    check(!reply.result && reply.moves?.length === 1 && !reply.copies?.length,
      role + ' expected exactly one moved non-domain session handle; result=' + reply.result);
    return reply.moves[0];
  };
  const scalar = async (label, handle, command, size) => {
    const reply = await step(label, () => s.ipc(label, handle, command, Buffer.alloc(0)));
    trackReplyHandles(reply, label + ' unexpected handle');
    check(!(reply.moves?.length || reply.copies?.length), label + ' unexpectedly returned handles');
    if (reply.result) return {available: false, result: reply.result, canonicalResult: reply.canonicalResult};
    check(Buffer.isBuffer(reply.data) && reply.data.length >= size, label + ' returned short inline data');
    return {available: true, bytes: reply.data.subarray(0, size)};
  };
  const nsInfo = async label => {
    if (options.nsVmHandle === undefined) return {available: false, reason: 'No owned ns:vm handle supplied'};
    const result = await scalar(label, options.nsVmHandle, NS_INFO_COMMAND, NS_INFO_BYTES);
    return result.available ? {available: true, info: decodeInfo(result.bytes)} : result;
  };
  const clockRead = async (label, handle) => {
    if (handle === undefined) return {available: false, reason: 'No network clock object available'};
    const result = await scalar(label, handle, 0, 8);
    return result.available ? {available: true, hex: result.bytes.toString('hex'),
      posixSeconds: result.bytes.readBigInt64LE().toString()} : result;
  };

  try {
    await fs.mkdir(outputDirectory, {recursive: true, mode: 0o700});
    directory = await fs.mkdtemp(path.join(outputDirectory, 'save49-' + live.session.replace(/[^a-zA-Z0-9_-]/g, '_') + '-'));
    receipt.directory = directory;
    await s.record({operation: 'cacheReadOnlyCaptureBegin', session: live.session, handlerSha256: receipt.handlerSha256});
    let clockHandle;
    if (options.timeStaticHandle !== undefined) {
      const reply = await step('time:GetStandardNetworkSystemClock command1', () =>
        s.ipc('Cache readonly: network clock object', options.timeStaticHandle, 1, Buffer.alloc(0)));
      if (reply.result) {
        trackReplyHandles(reply, 'failed clock open unexpected handle');
        check(!(reply.moves?.length || reply.copies?.length), 'Failed clock open unexpectedly returned handles');
        receipt.networkClockOpen = {available: false, result: reply.result, canonicalResult: reply.canonicalResult};
      } else {
        clockHandle = takeHandle(reply, 'network clock');
        receipt.networkClockOpen = {available: true, handle: clockHandle};
      }
    } else receipt.networkClockOpen = {available: false, reason: 'No owned time IStaticService supplied'};
    receipt.nsBefore = await nsInfo('Cache readonly: ns:vm3100 before');
    receipt.networkTimeBefore = await clockRead('Cache readonly: network time before', clockHandle);
    const opened = await step('FsProxy53 open readonly save49', () =>
      s.ipc('Cache readonly: OpenReadOnlySaveDataFileSystem49', options.rootHandle, 53, save49Input()));
    receipt.filesystemOpen = {result: opened.result, canonicalResult: opened.canonicalResult};
    const fsHandle = takeHandle(opened, 'save49 read-only filesystem');
    const file = await step('IFileSystem8 /entry mode1', () => filesystem.openFile(s, fsHandle, '/entry'));
    if (file.handle !== null && file.handle !== undefined) {
      check(!callerHandles.has(file.handle) && s.handles.has(file.handle) && !children.some(x => x.handle === file.handle),
        'Entry file handle is not a newly owned handle');
      children.push({handle: file.handle, role: 'entry read-only file'});
    }
    check(!file.result && Number.isInteger(file.handle) && file.handle > 0, 'Read-only /entry open failed: ' + file.result);
    const size = await step('IFile4 GetSize', () => filesystem.fileSize(s, file.handle));
    receipt.fileSize = BigInt(size).toString();
    check(BigInt(size) === BigInt(ENTRY_SIZE), 'Unexpected /entry size; capture stops without resizing');
    const first = await step('IFile0 Read4096 first plus native SHA verification', () =>
      filesystem.readFile(s, file.handle, 0, ENTRY_SIZE));
    check(Buffer.isBuffer(first) && first.length === ENTRY_SIZE, 'First entry read was short');
    const firstFile = path.join(directory, 'entry.first.bin');
    await writeExclusive(firstFile, first);
    receipt.reads.push({file: firstFile, size: first.length, sha256: sha256(first), nativeHashVerifiedByHelper: true});
    const second = await step('IFile0 Read4096 second plus native SHA verification', () =>
      filesystem.readFile(s, file.handle, 0, ENTRY_SIZE));
    check(Buffer.isBuffer(second) && second.length === ENTRY_SIZE, 'Second entry read was short');
    const secondFile = path.join(directory, 'entry.second.bin');
    await writeExclusive(secondFile, second);
    receipt.reads.push({file: secondFile, size: second.length, sha256: sha256(second), nativeHashVerifiedByHelper: true});
    receipt.entryStable = first.equals(second);
    receipt.entry = decodeEntry(first, options.firmware);
    check(receipt.entryStable, 'Cache changed between reads; both backups preserved');
    receipt.nsAfter = await nsInfo('Cache readonly: ns:vm3100 after');
    receipt.networkTimeAfter = await clockRead('Cache readonly: network time after', clockHandle);
    receipt.anchors = anchorAnalysis(receipt.entry, receipt.nsBefore, receipt.nsAfter);
    receipt.captureCompleted = true;
  } catch (error) {
    receipt.error = {message: error.message, atStep: lastStep ?? null};
    receipt.remoteOutcomeUncertain = !cleanupSafe;
  } finally {
    if (cleanupSafe && !s.threads.size && !s.pendingIpc) {
      for (const child of [...children].reverse()) {
        if (!s.handles.has(child.handle)) {
          receipt.cleanup.push({...child, closed: false, reason: 'Ownership disappeared; no close attempted'});
          continue;
        }
        try {
          await step('Close newly owned ' + child.role, () => s.close(child.handle));
          receipt.cleanup.push({...child, closed: true});
        } catch (error) {
          receipt.cleanup.push({...child, closed: false, error: error.message});
          receipt.error ||= {message: error.message, atStep: lastStep};
          receipt.remoteOutcomeUncertain = !cleanupSafe;
          break;
        }
      }
    }
    receipt.retainedNewHandles = [...s.handles].filter(handle => !callerHandles.has(handle)).map(handle =>
      children.find(child => child.handle === handle) || {handle, role: 'unclassified new handle; inspect before cleanup'});
    receipt.completed = receipt.captureCompleted === true && !receipt.error && !receipt.retainedNewHandles.length;
    receipt.finished = new Date().toISOString();
    receipt.furtherRpcSuppressed = !cleanupSafe;
    if (!cleanupSafe) {
      poisonedSessions.add(s);
      poisonedSessionIds.add(live.session);
      receipt.sessionPoisoned = true;
    }
    receipt.receiptFile = directory ? path.join(directory, 'receipt.json') : null;
    try {
      if (receipt.receiptFile) await writeExclusive(receipt.receiptFile, JSON.stringify(receipt, null, 2) + '\n');
      // The caller may attach this local receipt to its report; context.save is not invoked.
    } finally {activeSessions.delete(s);}
  }
  // Return the complete evidence/error receipt; the caller need not issue status RPC after an uncertain outcome.
  return receipt;
}

module.exports = {capture, save49Input, decodeEntry, decodeInfo, anchorAnalysis};
