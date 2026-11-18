'use strict';

const crypto = require('node:crypto');

const REQUEST_BYTES = 256;
const MAX_CANARY_BYTES = 0x800;
const sha256 = bytes => crypto.createHash('sha256').update(bytes).digest('hex');

function firstDifference(before, after) {
  if (before.length !== after.length) return Math.min(before.length, after.length);
  return before.findIndex((byte, index) => byte !== after[index]);
}

// Only read the current session's owned input and request page. Call this
// before workers are signaled or after every worker has joined.
async function capture(s, canaryAddress, canaryBytes, requestAddress) {
  if (!Number.isInteger(canaryBytes) || canaryBytes < 1 || canaryBytes > MAX_CANARY_BYTES)
    throw Error('Canary readback length is outside the bounded path buffer');
  const path = await s.readOwned(canaryAddress, canaryBytes);
  const request = await s.readOwned(requestAddress, REQUEST_BYTES);
  if (path.length !== canaryBytes || request.length !== REQUEST_BYTES)
    throw Error('Owned positive readback was short');
  return {path, request};
}

function describe(before, after) {
  if (!Buffer.isBuffer(before?.path) || !Buffer.isBuffer(before?.request) ||
      !Buffer.isBuffer(after?.path) || !Buffer.isBuffer(after?.request) ||
      before.path.length !== after.path.length ||
      before.request.length !== REQUEST_BYTES || after.request.length !== REQUEST_BYTES)
    throw Error('Positive readback snapshots differ in size');
  return {
    canaryPath: {bytes: before.path.length, beforeSha256: sha256(before.path),
      afterSha256: sha256(after.path), firstDifference: firstDifference(before.path, after.path),
      beforeHex: before.path.toString('hex'), afterHex: after.path.toString('hex')},
    canaryRequestPage: {bytes: REQUEST_BYTES, beforeSha256: sha256(before.request),
      afterSha256: sha256(after.request), beforeHex: before.request.toString('hex'),
      afterHex: after.request.toString('hex'), afterIsIpcReply: true,
      comparableAsPreparedRequest: false},
  };
}

function checkPrepared(before, expectedPath, expectedRequest) {
  if (!Buffer.isBuffer(before?.path) || !Buffer.isBuffer(before?.request) ||
      !Buffer.isBuffer(expectedPath) || !Buffer.isBuffer(expectedRequest) ||
      expectedRequest.length !== REQUEST_BYTES)
    throw Error('Positive prepared input is incomplete');
  if (!before.path.equals(expectedPath)) throw Error('Prepared positive canary path differs');
  if (!before.request.equals(expectedRequest)) throw Error('Prepared positive request page differs');
}

function fs6005Guard(row, windowQualified, state) {
  state ||= {};
  const reasons = [];
  if (!windowQualified) reasons.push('timing-window');
  if (row.canaryResult !== 0x2eea02 || row.canaryType !== null) reasons.push('canary-result');
  if (row.changedPaths?.length !== 1 || row.changedPaths[0] !== 'canary')
    reasons.push('changed-paths');
  const readback = row.positiveReadback;
  if (!readback || readback.beforeError || readback.afterError) reasons.push('readback-error');
  else if (readback.preparedPathMatches !== true || readback.preparedRequestMatches !== true ||
      readback.postPathMatchesPrepared !== true || readback.canaryPath?.firstDifference !== -1 ||
      readback.canaryPath?.beforeHex !== readback.canaryPath?.afterHex)
    reasons.push('readback-mismatch');
  if (!state.waveChecksPassed || !state.diskHealthy) reasons.push('wave-or-disk-check');
  if (!state.workersJoined || !state.groupClosed) reasons.push('workers-or-group');
  if (!state.ownedHandlesStable || !state.pinStillOwned) reasons.push('owned-handles-or-pin');
  if (!state.noCandidateCacheOrUncertainty) reasons.push('candidate-cache-or-uncertainty');
  return {eligible: reasons.length === 0, reasons};
}

// Exhaustion is a clean miss only when every observed wave returned an
// unchanged, ordinary canary and completed all of the existing owned-state
// checks. A timing-qualified window is not required for an aborted wave.
function cleanExhaustionWave(row, state) {
  const reasons = [];
  if (row?.restored !== true) reasons.push('allocation-restoration');
  if (row?.canaryResult !== 0 || row?.canaryType !== 1) reasons.push('canary-reply-or-type');
  if (!Array.isArray(row?.changedPaths) || row.changedPaths.length !== 0) reasons.push('changed-paths');
  if (row?.diskHealthy !== true || state?.diskHealthy !== true) reasons.push('disk-health');
  if (state?.waveChecksPassed !== true) reasons.push('wave-reply-checks');
  if (state?.workersJoined !== true || state?.groupClosed !== true) reasons.push('workers-or-group');
  if (state?.ownedHandlesStable !== true || state?.pinStillOwned !== true)
    reasons.push('owned-handles-or-pin');
  if (state?.noCandidateCacheOrUncertainty !== true) reasons.push('candidate-cache-or-uncertainty');
  return {eligible: reasons.length === 0, reasons};
}

async function persistFirstPositiveDecision(report, decision, save) {
  if (decision.kind === 'fs6005' && report.firstPositiveGuardPending !== true) {
    report.placementUncertain = true;
    throw Error('First-positive FS 6005 decision lacks its pending marker');
  }
  if (decision.kind === 'fs6005') {
    if (!decision.continued) report.placementUncertain = true;
    else report.firstPositiveGuardPending = false;
  } else if (decision.kind === 'selected-or-missed' || decision.kind === 'exhausted') {
    // Keep the terminal result pending until the caller checks strict 0/type0.
    report.firstPositiveGuardPending = true;
  } else {
    report.placementUncertain = true;
    throw Error('Unknown first-positive decision');
  }
  try { await save(); }
  catch (error) {
    report.firstPositiveGuardPending = true;
    report.placementUncertain = true;
    throw error;
  }
}

async function retryFirstPositive({nextWave, hasWindow, state, onWave, onDecision,
  initialAttempts = 30, maxAttempts = 60, maxContinuations = 2, extension = 15}) {
  let allowed = initialAttempts, attempts = 0, continuations = 0, row;
  const uncertainWaves = [];
  while (attempts < allowed) {
    row = await nextWave();
    attempts++;
    await onWave(attempts);
    const waveState = state(row);
    const clean = cleanExhaustionWave(row, waveState);
    if (!clean.eligible) uncertainWaves.push({wave: attempts, reasons: clean.reasons});
    if (row.canaryResult === 0x2eea02) {
      const guard = fs6005Guard(row, hasWindow(row), waveState);
      const reasons = [...guard.reasons];
      if (continuations >= maxContinuations) reasons.push('continuation-cap');
      if (attempts >= maxAttempts) reasons.push('wave-cap');
      const continued = reasons.length === 0;
      if (continued) {
        continuations++;
        allowed = Math.min(maxAttempts, allowed + extension);
      }
      await onDecision({kind: 'fs6005', attempts, allowed, continuations, continued, reasons});
      if (continued) continue;
      return row;
    }
    if (!hasWindow(row)) continue;
    if (row.canaryResult === 0 && row.canaryType === 1 && row.changedPaths.length === 0) continue;
    await onDecision({kind: 'selected-or-missed', attempts, allowed, continuations,
      continued: false, reasons: []});
    return row;
  }
  const terminalAborted = row?.triggerAborted === true;
  const decision = {kind: 'exhausted', attempts, allowed, continuations,
    continued: false, reasons: ['wave-limit', ...(terminalAborted ? [] : ['terminal-not-aborted'])],
    terminalAborted, cleanupEligible: uncertainWaves.length === 0 && terminalAborted,
    uncertainWaves};
  await onDecision(decision);
  // Do not let the final row's abort flag bypass the strict selection gate.
  return {...decision, lastRow: row};
}

async function requireFirstPositiveRow(outcome, report, save) {
  if (outcome?.kind !== 'exhausted') {
    if (outcome?.kind !== undefined || outcome?.profile !== 'positive') {
      report.placementUncertain = true;
      throw Error('First-positive terminal row is missing or invalid');
    }
    return outcome;
  }
  const clean = outcome.cleanupEligible === true && Array.isArray(outcome.uncertainWaves) &&
    outcome.uncertainWaves.length === 0 && outcome.lastRow?.triggerAborted === true &&
    outcome.terminalAborted === true && outcome.attempts > 0 &&
    report.firstPositiveGuardPending === true && report.placementUncertain !== true;
  report.firstPositiveExhaustion = {attempts: outcome.attempts, allowed: outcome.allowed,
    continuations: outcome.continuations, terminalAborted: outcome.terminalAborted,
    cleanupEligible: clean,
    uncertainWaves: outcome.uncertainWaves};
  if (clean) report.firstPositiveGuardPending = false;
  else report.placementUncertain = true;
  try { await save(); }
  catch (error) {
    report.firstPositiveGuardPending = true;
    report.placementUncertain = true;
    throw error;
  }
  const detail = clean ? 'owned cleanup allowed after every wave passed its checks' :
    'placement uncertain; retain resources for recovery';
  throw Error('First-positive waves exhausted after ' + outcome.attempts +
    ' attempts without strict 0/type0 canary selection; ' + detail);
}

module.exports = {capture, describe, checkPrepared, fs6005Guard, cleanExhaustionWave,
  persistFirstPositiveDecision, retryFirstPositive, requireFirstPositiveRow,
  firstDifference, MAX_CANARY_BYTES, REQUEST_BYTES};
