'use strict';

// A failed probe is normally terminal. This one pre-cache measurement can be
// repeated only after the probe itself has finished and released all owners.
function qualifyCleanCandidateRetry(report, session, exit) {
  const reasons = [];
  const requireThat = (ok, reason) => { if (!ok) reasons.push(reason); };
  requireThat(exit?.code === 1 && exit?.signal === null, 'probe-exit');
  requireThat(report?.session === session, 'session');
  requireThat(typeof report?.error === 'string' &&
    report.error.startsWith('Error: Candidate allocation cost differs\n'), 'error');
  requireThat(report?.cacheAlias?.allocationBytes === 13632, 'allocation');
  requireThat(report?.cacheAlias?.attempts === undefined && !report?.cacheRange &&
    !report?.cacheAddress && !report?.memoryControl?.ready, 'pre-cache-stage');
  requireThat(report?.cleanedUp === true && !!report?.finished &&
    !report?.cleanupError && !report?.placementUncertain && !report?.stopped,
  'finished-cleanup');
  requireThat(report?.control?.mode === 'closed' && report?.control?.active === false,
    'control');
  const retained = report?.retained;
  requireThat(retained?.released === true && retained?.base === 0 &&
    retained?.cacheNeedsRestore === false && retained?.group == null &&
    Array.isArray(retained?.handles) && retained.handles.length === 0 &&
    Array.isArray(retained?.threads) && retained.threads.length === 0 &&
    Array.isArray(retained?.created) && retained.created.length === 0,
  'owned-resources');
  requireThat(Array.isArray(retained?.workers) && retained.workers.length === 13 &&
    retained.workers.every(worker => worker.joined === true), 'workers');
  requireThat(Array.isArray(report?.cases) && report.cases.length > 0 &&
    report.cases.every(row => row.restored === true && row.diskHealthy === true),
  'wave-restoration');
  return {eligible: reasons.length === 0, reasons};
}

module.exports = {qualifyCleanCandidateRetry};
