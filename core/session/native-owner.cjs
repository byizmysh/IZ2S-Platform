'use strict';
// Opt-in HOST bookkeeping only: no native calls, signals, reconnects or replay.
// This file records a trusted controller's observations; it cannot verify SDK
// evidence itself. A dead PID, old timestamp or timeout never proves cleanup.
// Publish arm(identity, {sdHandle, config, report}) before starting the native
// consumer. After actual finished+join and zero resource counters, record
// joinedClean(identity, proof). Host cleanup obtains acquireCleanup(identity),
// performs its own cleanup inside try/finally, then releases that lease. Clear
// the terminal record only after all host cleanup succeeds. Without integration
// at these boundaries this helper changes no existing session behaviour.
const fs = require('node:fs/promises');
const constants = require('node:fs').constants;
const path = require('node:path');
const crypto = require('node:crypto');
const FILE = 'native-owner.json';
const FORMAT = 'iz-native-owner-v1';
const phases = new Set(['armed', 'ready', 'retained', 'joined-clean']);
const zeroFields = ['nativeError', 'jitMapped', 'mappingUnknown', 'audioOpened',
  'audioPending', 'audioUncertain', 'filesOpened', 'sdLeakedHandle'];
const positive = value => Number.isSafeInteger(value) && value > 0;
const plain = value => value !== null && typeof value === 'object' && !Array.isArray(value);
function check(ok, message) { if (!ok) throw Error(message); }
function sessionValid(session) { return typeof session === 'string' && /^launcher-[A-Za-z0-9]+$/.test(session); }
function identityValid(identity) {
  return plain(identity) && positive(identity.ownerPid) && identity.ownerPid <= 0x7fffffff && typeof identity.token === 'string' &&
    /^(?:[a-f0-9]{32}|[a-f0-9]{48}|[a-f0-9]{64})$/.test(identity.token);
}
function matches(record, identity) {
  return identityValid(identity) && record.token === identity.token && record.ownerPid === identity.ownerPid;
}
function proofValid(proof) {
  return plain(proof) && proof.threadFinished === true && proof.threadJoined === true &&
    proof.ownershipKnown === true && proof.returnCode === '0' &&
    zeroFields.every(key => proof[key] === 0);
}
function recordValid(record, session) {
  return plain(record) && record.format === FORMAT && record.version === 1 &&
    sessionValid(record.session) && record.session === session && identityValid(record) &&
    phases.has(record.phase) && positive(record.revision) && record.ownsPromotedFs === true &&
    plain(record.resources) && positive(record.resources.sdHandle) &&
    record.resources.sdHandle <= 0x7fffffff && positive(record.resources.config) &&
    record.resources.config < 2 ** 39 && record.resources.config % 8 === 0 && typeof record.resources.report === 'string' &&
    path.isAbsolute(record.resources.report) &&
    (record.phase === 'joined-clean' ? proofValid(record.proof) : record.proof === undefined);
}
function retainDecision(reason, phase = 'unknown') {
  return {cleanupAllowed: false, retainResources: true, phase, reason};
}
// A present but unrecognised record always retains. An absent record preserves
// legacy behaviour only when the caller has no previously accepted identity.
function evaluate(snapshot, {session, identity} = {}) {
  check(sessionValid(session), 'Invalid native owner session');
  if (identity !== undefined && !identityValid(identity)) return retainDecision('invalid-expected-identity');
  if (snapshot?.state === 'absent') {
    if (identity !== undefined) return retainDecision('expected-owner-record-missing');
    return {cleanupAllowed: true, retainResources: false, phase: 'absent', reason: 'not-opted-in'};
  }
  if (snapshot?.state !== 'present' || !recordValid(snapshot.record, session))
    return retainDecision('owner-record-unverified');
  const record = snapshot.record;
  if (identity !== undefined && !matches(record, identity)) return retainDecision('owner-identity-mismatch', record.phase);
  if (record.phase !== 'joined-clean') return retainDecision('native-owner-retains-resources', record.phase);
  if (!identity) return retainDecision('clean-proof-needs-exact-identity', record.phase);
  return {cleanupAllowed: true, retainResources: false, phase: record.phase, reason: 'matching-joined-clean-proof'};
}
function create({directory, session}) {
  check(typeof directory === 'string' && path.isAbsolute(directory), 'Expected absolute native owner directory');
  check(sessionValid(session), 'Invalid native owner session');
  const file = path.join(directory, FILE), lock = file + '.lock';
  async function readRecord() {
    let handle;
    try {
      handle = await fs.open(file, constants.O_RDONLY | constants.O_NOFOLLOW);
      const stat = await handle.stat();
      if (!stat.isFile() || stat.size > 65536) return {state: 'unknown', reason: 'nonregular-or-oversized-record'};
      const record = JSON.parse(await handle.readFile('utf8'));
      if (!recordValid(record, session)) return {state: 'unknown', reason: 'invalid-owner-record'};
      return {state: 'present', record};
    } catch (error) {
      return error.code === 'ENOENT' ? {state: 'absent'} :
        {state: 'unknown', reason: 'unreadable-owner-record', code: error.code || 'INVALID_JSON'};
    } finally { await handle?.close(); }
  }
  async function lockPresent() {
    try {await fs.lstat(lock); return true;}
    catch (error) {if (error.code === 'ENOENT') return false; throw error;}
  }
  async function read() {
    try {
      if (!(await fs.stat(directory)).isDirectory()) return {state: 'unknown', reason: 'invalid-owner-directory'};
      if (await lockPresent()) return {state: 'unknown', reason: 'owner-update-in-progress'};
      const snapshot = await readRecord();
      if (await lockPresent()) return {state: 'unknown', reason: 'owner-update-in-progress'};
      return snapshot;
    } catch (error) {return {state: 'unknown', reason: 'owner-lock-unreadable', code: error.code};}
  }
  async function locked(operation) {
    // Never steal a lock by age or PID liveness: an interrupted writer is an
    // uncertain ownership state, requiring explicit investigation.
    await fs.mkdir(lock, {mode: 0o700});
    try { return await operation(); }
    finally { await fs.rmdir(lock); }
  }
  async function save(record) {
    const temporary = file + '.' + crypto.randomUUID() + '.tmp';
    let handle;
    try {
      handle = await fs.open(temporary, 'wx', 0o600);
      await handle.writeFile(JSON.stringify(record, null, 2) + '\n');
      await handle.sync(); await handle.close(); handle = null;
      await fs.rename(temporary, file);
    } finally {
      await handle?.close();
      await fs.unlink(temporary).catch(error => {if (error.code !== 'ENOENT') throw error;});
    }
    return record;
  }
  async function own(identity) {
    check(identityValid(identity), 'Invalid native owner identity');
    const snapshot = await readRecord();
    check(snapshot.state === 'present', 'Native owner record is missing or unverified; retain resources');
    check(matches(snapshot.record, identity), 'Native owner identity differs; retain resources');
    return snapshot.record;
  }
  async function transition(identity, phase, details) {
    return locked(async () => {
      const record = await own(identity);
      check(record.phase !== 'joined-clean', 'Joined-clean native owner is terminal');
      if (phase === 'ready') check(record.phase === 'armed', 'Only an armed owner can become ready');
      const next = {...record, ...details, phase, revision: record.revision + 1, updated: new Date().toISOString()};
      check(recordValid(next, session), 'Invalid native owner transition');
      return save(next);
    });
  }
  return {
    file, read,
    // Advisory snapshot for status/UI. Actual cleanup must acquire the lease
    // below, preventing an arm() between a no-owner decision and destruction.
    async decision(identity) { return evaluate(await read(), {session, identity}); },
    async acquireCleanup(identity) {
      try {await fs.mkdir(lock, {mode: 0o700});}
      catch (error) {return {...retainDecision('owner-cleanup-lock-unavailable'), code: error.code, release: null};}
      let decision;
      try {decision = evaluate(await readRecord(), {session, identity});}
      catch (error) {await fs.rmdir(lock); throw error;}
      if (!decision.cleanupAllowed) {await fs.rmdir(lock); return {...decision, release: null};}
      let releasing;
      return {...decision, release() {
        if (!releasing) releasing = fs.rmdir(lock);
        return releasing;
      }};
    },
    async arm(identity, resources) {
      check(identityValid(identity), 'Invalid native owner identity');
      const record = {format: FORMAT, version: 1, session, token: identity.token, ownerPid: identity.ownerPid,
        phase: 'armed', revision: 1, ownsPromotedFs: true,
        resources: {sdHandle: resources?.sdHandle, config: resources?.config, report: resources?.report},
        created: new Date().toISOString(), updated: new Date().toISOString()};
      check(recordValid(record, session), 'Invalid native owner resources');
      return locked(async () => {
        check((await readRecord()).state === 'absent', 'Native owner already exists or is unverified; retain resources');
        return save(record);
      });
    },
    async ready(identity) {return transition(identity, 'ready');},
    async retain(identity, reason) {
      check(typeof reason === 'string' && reason.length > 0 && reason.length <= 1600, 'Expected native retention reason');
      return transition(identity, 'retained', {reason});
    },
    async joinedClean(identity, proof) {
      check(proofValid(proof), 'Native cleanup needs explicit finished/joined/zero-ownership proof');
      // Copy only the documented proof fields; callers cannot inject identities.
      const clean = {threadFinished: true, threadJoined: true, ownershipKnown: true, returnCode: '0'};
      for (const key of zeroFields) clean[key] = proof[key];
      return transition(identity, 'joined-clean', {proof: clean});
    },
    // Call only AFTER actual host cleanup succeeds. Keeping the terminal record
    // until then prevents a second owner replacing resources still in cleanup.
    async clearJoinedClean(identity) {
      return locked(async () => {
        const record = await own(identity);
        check(record.phase === 'joined-clean' && proofValid(record.proof), 'Cannot clear a retained native owner');
        await fs.unlink(file); return record;
      });
    },
  };
}
module.exports = {FILE, FORMAT, create, evaluate, recordValid, proofValid};
