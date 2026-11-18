'use strict';
// In-process dispatch authority. This fences requests; it does not establish
// kernel ownership, installation provenance, native completion or a live lease.
const crypto = require('node:crypto');
const secret = () => crypto.randomBytes(32).toString('hex');
const opaque = value => typeof value === 'string' && /^[A-Za-z0-9_-]{1,128}$/.test(value);
const digest = value => typeof value === 'string' && /^[a-f0-9]{64}$/.test(value);
function check(ok, message) { if (!ok) throw Error(message); }

function create({drain, verifyRelease} = {}) {
  check(typeof drain === 'function', 'Admission requires the actual queue drain');
  check(verifyRelease === undefined || typeof verifyRelease === 'function', 'Invalid trusted release verifier');
  const authorityId = crypto.randomUUID(), permits = new Map(), waiters = new Set();
  let phase = 'open', generation = 0, owner = null, tickets = 0, reasonCode = null;
  let shutdownTask = null, shutdownSeal = null;
  let cleanupIssued = false, cleanupPermit = null;
  const wake = () => { for (const resolve of waiters) resolve(); waiters.clear(); };
  const changed = () => new Promise(resolve => waiters.add(resolve));
  const metadata = () => ({authorityId, ownerId: owner.ownerId, generation: owner.generation});
  const inspect = () => ({authorityId, phase, generation, participantCount: permits.size,
    admittedRequests: tickets, reasonCode, scope: 'sdk-http-dispatch', hardwareLease: false});
  function matches(claim) {
    return claim && owner && claim.authorityId === authorityId && claim.ownerId === owner.ownerId &&
      claim.generation === owner.generation && claim.capability === owner.capability;
  }
  function retain(code = 'caller-retained') {
    phase = 'retained'; reasonCode = code; wake();
    return inspect(); // Never include caller reason text or capabilities.
  }
  async function drained(expected) {
    for (;;) {
      check(phase === expected, 'Admission retained or changed while draining');
      if (tickets === 0 && permits.size === 0) {
        await drain();
        check(phase === expected, 'Admission retained or changed while draining');
        if (tickets === 0 && permits.size === 0) return;
      }
      await changed();
    }
  }
  async function acquire({ownerId} = {}) {
    check(opaque(ownerId) && phase === 'open', 'Admission cannot acquire this owner/state');
    phase = 'draining'; reasonCode = null; wake(); // Fence BEFORE the first await.
    try {
      await drained('draining');
      owner = {authorityId, ownerId, generation: ++generation, capability: secret()};
      phase = 'active'; wake(); return {...owner};
    } catch (error) { retain('acquire-incomplete'); throw error; }
  }
  function begin({participantId, claim} = {}) {
    check(opaque(participantId) && permits.size < 64, 'Invalid or excessive admission participant');
    check((phase === 'open' && claim === undefined) || (phase === 'active' && matches(claim)),
      'Admission participant is fenced');
    const permit = secret();
    permits.set(permit, {participantId, generation, ownerBound: phase === 'active', inflight: 0, ending: false});
    wake(); return {authorityId, generation, permit};
  }
  function tryBegin({participantId, claim} = {}) {
    check(opaque(participantId), 'Invalid admission participant');
    // The refusal and possible grant are one synchronous authority operation.
    // A separate status/read followed by BEGIN would race owner acquisition.
    if (permits.size >= 64 || !((phase === 'open' && claim === undefined) || (phase === 'active' && matches(claim))))
      return {begun: false, fenced: true, authorityId, generation, phase};
    return {begun: true, phase, ...begin({participantId, claim})};
  }
  function end({permit} = {}) {
    const entry = permits.get(permit);
    check(entry && !entry.ending, 'Unknown or ending admission participant');
    if (entry.shutdownCleanup) {
      retain('cleanup-permit-ended-via-general-control');
      throw Error('Shutdown cleanup permit requires the original host completion boundary');
    }
    if (entry.inflight) {
      retain('participant-ended-with-inflight-dispatch');
      throw Error('Admission participant still has queued or inflight SDK requests; retained');
    }
    entry.ending = true;
    // Ending a dispatch never releases a retained authority or claims native
    // thread completion. A failed participant must explicitly retain first.
    permits.delete(permit); wake(); return {ended: true};
  }
  function admit(headers = {}) {
    const capability = headers['x-iz-admission'], epoch = headers['x-iz-admission-generation'];
    const permit = headers['x-iz-admission-permit'];
    check(!(permit !== undefined && (capability !== undefined || epoch !== undefined)), 'Ambiguous admission credentials');
    let participant = null, kind;
    if (permit !== undefined) {
      participant = permits.get(permit);
      check(participant && !participant.ending && phase !== 'retained', 'Admission participant is fenced');
      check(participant.generation === generation, 'Stale admission participant');
      kind = 'participant'; participant.inflight++;
    } else if (capability !== undefined || epoch !== undefined) {
      check(phase === 'active' && owner && capability === owner.capability && epoch === String(owner.generation),
        'Admission owner is fenced');
      kind = 'owner';
    } else {
      check(phase === 'open', 'Admission dispatch is fenced'); kind = 'ordinary';
    }
    tickets++; let done = false;
    return {
      beforeDispatch() {
        check(!done && phase !== 'retained', 'Admission dispatch retained');
        // Ordinary requests accepted before acquire drain normally. Existing
        // participant SDK calls may continue during drain/release; this lets
        // an already-running external FS dispatch finish its nested RPCs.
        if (kind === 'owner') check(owner && ['active', 'releasing'].includes(phase), 'Admission owner changed');
      },
      failed() {
        if (kind !== 'ordinary' || phase !== 'open') retain('admitted-dispatch-failed');
      },
      finish() {
        if (done) return;
        done = true; tickets--;
        if (participant) participant.inflight--;
        wake();
      },
    };
  }
  async function release({claim, receipt} = {}) {
    check(phase === 'active' && matches(claim), 'Admission release owner differs');
    phase = 'releasing'; wake();
    try {
      await drained('releasing');
      check(typeof verifyRelease === 'function', 'No trusted admission cleanup verifier configured');
      const expected = metadata(), proof = await verifyRelease({claim: expected, receipt});
      check(phase === 'releasing' && proof && proof.kind === 'verified-admission-cleanup' &&
        proof.authorityId === expected.authorityId && proof.ownerId === expected.ownerId &&
        proof.generation === expected.generation && digest(proof.evidenceSha256),
        'Trusted admission cleanup receipt differs');
      owner = null; phase = 'open'; wake();
      return {released: true, authorityId, generation, evidenceSha256: proof.evidenceSha256, hardwareLease: false};
    } catch (error) { retain('release-unverified'); throw error; }
  }
  function callerRetain({claim, permit} = {}) {
    check((claim !== undefined && matches(claim)) || (permit !== undefined && permits.has(permit)) ||
      (claim === undefined && permit === undefined), 'Admission retain credentials differ');
    return retain(); // This method is available only on the trusted control channel.
  }
  function verifyShutdownSeal(receipt) {
    // The SAME in-memory object is the capability. JSON copies, hashes and
    // cleanup booleans cannot authorise original-host teardown.
    return shutdownSeal !== null && receipt === shutdownSeal && phase === 'closed-clean' &&
      owner === null && tickets === 0 && permits.size === 0;
  }
  function sealForShutdown() {
    if (shutdownTask) return shutdownTask; // No retry or regeneration after uncertainty.
    if (phase !== 'open') {
      retain('shutdown-with-live-admission');
      shutdownTask = Promise.resolve(Object.freeze({kind: 'dispatch-retained', authorityId,
        dispatchCleanupAllowed: false, nativeCompletion: false}));
      return shutdownTask;
    }
    phase = 'closing'; wake(); // Fence synchronously BEFORE the first await.
    shutdownTask = (async () => {
      try {
        // Already accepted ordinary requests and existing participant nested
        // RPCs can drain; no new participant, owner or ordinary request enters.
        await drained('closing');
        shutdownSeal = Object.freeze({kind: 'sealed-dispatch-cleanup', authorityId, generation,
          dispatchCleanupAllowed: true, nativeCompletion: false});
        phase = 'closed-clean'; wake(); return shutdownSeal;
      } catch {
        retain('shutdown-drain-unverified');
        return Object.freeze({kind: 'dispatch-retained', authorityId,
          dispatchCleanupAllowed: false, nativeCompletion: false});
      }
    })();
    return shutdownTask;
  }
  function beginShutdownCleanup(receipt, {participantId} = {}) {
    check(verifyShutdownSeal(receipt) && !cleanupIssued && opaque(participantId),
      'Original sealed shutdown cleanup boundary is required');
    cleanupIssued = true;
    cleanupPermit = secret();
    permits.set(cleanupPermit, {participantId, generation, shutdownCleanup: true, inflight: 0, ending: false});
    phase = 'cleanup'; wake();
    return Object.freeze({authorityId, generation, permit: cleanupPermit});
  }
  function verifyCleanupPermit({authorityId: requestedAuthority, generation: requestedGeneration, permit} = {}) {
    const entry = permits.get(permit);
    check(phase === 'cleanup' && requestedAuthority === authorityId && requestedGeneration === generation &&
      permit === cleanupPermit && entry?.shutdownCleanup === true && !entry.ending,
      'Original shutdown cleanup permit differs');
    return {verified: true, phase, authorityId, generation};
  }
  async function endShutdownCleanup(receipt, {permit} = {}) {
    const entry = permits.get(permit);
    try {
      check(receipt === shutdownSeal && shutdownSeal !== null && phase === 'cleanup' &&
        permit === cleanupPermit && entry?.shutdownCleanup === true && !entry.ending &&
        permits.size === 1 && tickets === 0 && entry.inflight === 0,
        'Shutdown cleanup still pending or completion boundary differs');
      entry.ending = true; // No new request can enter while the real queue drains.
      await drain();
      check(phase === 'cleanup' && tickets === 0 && entry.inflight === 0 && permits.size === 1,
        'Shutdown cleanup changed during final drain');
      permits.delete(permit); cleanupPermit = null; phase = 'closed-clean'; wake();
      check(verifyShutdownSeal(receipt), 'Final shutdown seal is not valid');
      return {completed: true, authorityId, generation, nativeCompletion: false};
    } catch (error) {retain('shutdown-cleanup-unverified'); throw error;}
  }
  return {acquire, begin, tryBegin, end, release, retain: callerRetain, inspect, admit,
    sealForShutdown, verifyShutdownSeal, beginShutdownCleanup, verifyCleanupPermit, endShutdownCleanup,
    close() {
      // Closing HTTP alone never creates a cleanup capability. Preserve only
      // one already issued by successful original-host shutdown drain.
      if (!verifyShutdownSeal(shutdownSeal)) retain('bridge-closed');
    }};
}
module.exports = {create};
