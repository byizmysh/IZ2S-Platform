'use strict';
// Host-side cleanup boundary. Parking preserves the parent IPC channel of the
// held FS child. It makes no native requests, signals, reconnects or replays.
const path = require('node:path');
const owners = require('./native-owner.cjs');
const {saveJson, redact} = require('./ledger.cjs');
const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
async function finish({directory, session, bridge, cleanup, reason = 'host-stopped',
  record = {}, onRetain = async () => {}, signals = process, pause = sleep,
  owner = owners.create({directory, session}), interval = 250, prepareCleanup}) {
  if (typeof cleanup !== 'function' || !Number.isInteger(interval) || interval < 1)
    throw Error('Invalid native owner host boundary');
  const admission = bridge?.admission;
  let admissionReceipt, admissionError, admissionSettled;
  if (admission) {
    // Original serve's in-memory authority, not an SD owner record. Seal is
    // invoked synchronously BEFORE this function's first await, so a new HTTP
    // claim/SDK dispatch cannot race the host's marker-absent cleanup path.
    try {
      if (typeof admission.sealForShutdown !== 'function' || typeof admission.verifyShutdownSeal !== 'function')
        throw Error('Original bridge shutdown sealing is unavailable');
      admissionSettled = Promise.resolve(admission.sealForShutdown()).then(
        receipt => {admissionReceipt = receipt;}, error => {admissionError = error;});
    } catch (error) {admissionError = error; admissionSettled = Promise.resolve();}
  }
  const file = path.join(directory, 'native-owner-host-state.json');
  let identity, observedOwner = false, parked = false, announced = false,
    bridgeClosing, bridgeDone = false, bridgeError, signalCount = 0, lastSignal = null, lastState = '';
  let endpointCleanupPrepared = false, cleanupFailed = null;
  const leaseReleases = new WeakMap();
  // Install before the first await: a second STOP cannot restore default
  // termination while an uncertain owner still depends on this parent process.
  const stop = signal => {signalCount++; lastSignal = signal;};
  const interrupt = () => stop('SIGINT'), terminate = () => stop('SIGTERM');
  signals.on('SIGINT', interrupt); signals.on('SIGTERM', terminate);
  async function persist(state) {
    const status = {format: 'iz-native-owner-host-v1', session, ...state,
      signalCount, lastSignal, automaticReconnect: false};
    record.nativeOwner = status;
    const key = JSON.stringify(status);
    if (key === lastState) return;
    try {await saveJson(file, {...status, at: new Date().toISOString()}); lastState = key;}
    catch (error) {record.nativeOwnerStatusError = error.message;}
  }
  function closeLocalBridge() {
    if (bridgeClosing) return;
    // The shared bridge's close clears its heartbeat and HTTP listener, then
    // waits for already-enqueued work. It creates no native request itself.
    // Never retry it: an in-flight operation may have completed on the console.
    bridgeClosing = Promise.resolve().then(() => bridge?.close()).catch(error => {
      bridgeError = error; record.nativeOwnerBridgeError = error.message;
    }).finally(() => {bridgeDone = true;});
  }
  async function park(decision) {
    parked = true;
    // A verified dispatch seal blocks ordinary work. Keep that sealed HTTP
    // route available while waiting for legacy owner proof: the later, solely
    // authorized FS restoration still needs SDK calls through a cleanup permit.
    if (!prepareCleanup || !admission || cleanupFailed || !admissionSealed()) closeLocalBridge();
    await persist({state: 'parked', phase: decision.phase, reason: redact(reason),
      policyReason: decision.reason, resourcesRetained: true, cleanupAllowed: false,
      endpointParentRetained: true, consoleExecution: 'unverified after host shutdown request',
      ...(bridgeError ? {bridgeError: bridgeError.message} : {})});
    if (!announced) {
      announced = true;
      // A reporting failure must not terminate this held endpoint's parent.
      try {await onRetain(record.nativeOwner);} catch (error) {record.nativeOwnerStateError = error.message;}
    }
    // This ref'ed timer keeps the host alive even if its network socket and
    // bridge have closed. STOP/PID death/timeouts never break the wait.
    await pause(interval);
  }
  function admissionSealed() {
    if (!admission) return true;
    try {return !admissionError && admission.verifyShutdownSeal(admissionReceipt) === true;}
    catch (error) {admissionError = error; return false;}
  }
  async function releaseCleanupLease(lease) {
    let attempt = leaseReleases.get(lease);
    if (!attempt) {
      attempt = Promise.resolve().then(() => lease.release()).then(
        () => ({ok: true}), () => ({ok: false}));
      leaseReleases.set(lease, attempt);
    }
    if ((await attempt).ok) return;
    cleanupFailed ||= Error('Cleanup lease release unverified');
    try {admission?.retain({});} catch { /* Keep the original parent even if retention reporting fails. */ }
    // Retain the lock and parent. A rejected rmdir is not permission to retry
    // cleanup or escape a finally block after an uncertain native operation.
    for (;;) await park({phase: 'owner-lease-retained', reason: 'cleanup-lease-release-unverified'});
  }
  try {
    for (;;) {
      if (cleanupFailed) {
        await park({phase: 'admission-cleanup-retained', reason: 'original-host-cleanup-outcome-unknown'}); continue;
      }
      if (admission) {
        await admissionSettled;
        if (!admissionSealed()) {
          // No disappearance of a host marker, process exit or timeout can
          // promote this authority to clean after a failed/uncertain seal.
          await park({phase: 'admission-retained', reason: 'original-bridge-shutdown-unverified'}); continue;
        }
      }
      let snapshot;
      try {snapshot = await owner.read();}
      catch {snapshot = {state: 'unknown'};}
      if (snapshot.state !== 'absent') observedOwner = true;
      if (!identity && snapshot.state === 'present' && owners.recordValid(snapshot.record, session))
        identity = {token: snapshot.record.token, ownerPid: snapshot.record.ownerPid};
      // Once a marker or uncertain write was observed, deleting it cannot turn
      // this boundary back into the legacy no-owner path.
      if (observedOwner && !identity) {
        await park({phase: 'unknown', reason: snapshot.state === 'absent' ?
          'observed-owner-record-disappeared' : 'owner-record-unverified'}); continue;
      }
      // Active/unknown snapshots cannot authorise cleanup. Avoid taking their
      // writer lock merely to rediscover that fact, so the controller can
      // publish its eventual joined-clean observation without polling races.
      const observed = owners.evaluate(snapshot, {session, identity});
      if (!observed.cleanupAllowed) {await park(observed); continue;}
      let lease;
      try {lease = await owner.acquireCleanup(identity);}
      catch {lease = {cleanupAllowed: false, phase: 'unknown', reason: 'owner-cleanup-lease-failed'};}
      if (!lease.cleanupAllowed) {observedOwner = true; await park(lease); continue;}
      if (admission) {
        if (prepareCleanup && !endpointCleanupPrepared) {
          try {
            if (typeof admission.beginShutdownCleanup !== 'function' || typeof admission.endShutdownCleanup !== 'function')
              throw Error('Original host cleanup participant is unavailable');
            const scope = admission.beginShutdownCleanup(admissionReceipt, {participantId: 'fs-endpoint-restoration'});
            await prepareCleanup({admissionCleanup: scope});
            await admission.endShutdownCleanup(admissionReceipt, {permit: scope.permit});
            endpointCleanupPrepared = true;
          } catch (error) {
            cleanupFailed = error;
            admission.retain({});
            await releaseCleanupLease(lease);
            await park({phase: 'admission-cleanup-retained', reason: 'endpoint-restoration-unverified'}); continue;
          }
        }
        closeLocalBridge();
        await bridgeClosing;
        if (bridgeError || !admissionSealed()) {
          await releaseCleanupLease(lease);
          await park({phase: 'admission-retained', reason: bridgeError ?
            'sealed-bridge-close-unverified' : 'original-bridge-shutdown-seal-changed'}); continue;
        }
      }
      if (parked) {
        // Joining the native consumer does not resolve an outstanding bridge
        // request. Wait for its local close before releasing shared services.
        if (!bridgeDone) {
          await releaseCleanupLease(lease);
          await park({phase: lease.phase, reason: 'local-bridge-close-pending'}); continue;
        }
        if (bridgeError) {
          await releaseCleanupLease(lease);
          await park({phase: lease.phase, reason: 'local-bridge-close-unverified'}); continue;
        }
        await persist({state: 'cleanup-permitted', phase: lease.phase, resourcesRetained: true,
          cleanupAllowed: true, policyReason: lease.reason, endpointParentRetained: true});
      }
      try {
        // Lease stays locked through the actual endpoint/native cleanup. A new
        // arm cannot race a previously observed absent or joined-clean marker.
        const result = await cleanup({bridgeAlreadyClosing: parked || !!admission, identity, endpointAlreadyClosed: endpointCleanupPrepared});
        if (parked) await persist({state: 'cleaned', phase: lease.phase, resourcesRetained: false,
          cleanupAllowed: true, policyReason: lease.reason, endpointParentRetained: false});
        return {parked, result, identity};
      } catch (error) {
        if (admission) {
          // The admitted cleanup may have reached native code. Preserve the
          // original parent and never retry cleanup/native.stop after failure.
          cleanupFailed = error; admission.retain({});
          await persist({state: 'cleanup-retained', phase: lease.phase, resourcesRetained: true,
            cleanupAllowed: false, endpointParentRetained: true});
          continue;
        }
        if (parked) await persist({state: 'cleanup-failed', phase: lease.phase, resourcesRetained: true,
          cleanupAllowed: false, error: error.message, endpointParentRetained: false});
        throw error;
      } finally {await releaseCleanupLease(lease);}
    }
  } finally {
    signals.removeListener('SIGINT', interrupt); signals.removeListener('SIGTERM', terminate);
  }
}
module.exports = {finish};
