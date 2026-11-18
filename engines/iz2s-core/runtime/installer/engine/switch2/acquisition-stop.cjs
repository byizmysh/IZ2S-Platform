'use strict';

const ACQUISITION_TIMEOUT_MS = 25 * 60 * 1000;
const STOP_GRACE_MS = 4 * 60 * 1000;
const KILL_GRACE_MS = 30 * 1000;

async function exitedWithin(done, timeoutMs) {
  if (!Number.isSafeInteger(timeoutMs) || timeoutMs < 1) throw Error('Invalid child stop deadline');
  let timer;
  try {
    return await Promise.race([done.then(() => true), new Promise(resolve => {
      timer = setTimeout(() => resolve(false), timeoutMs);
    })]);
  } finally { clearTimeout(timer); }
}

// The probe's RunControl handles SIGTERM at its next checkpoint and runs its
// existing restoration path. A hard kill is only the bounded fallback.
async function stopChildAtDeadline(child, done,
  {stopGraceMs = STOP_GRACE_MS, killGraceMs = KILL_GRACE_MS} = {}) {
  const sigtermSent = child.kill('SIGTERM');
  if (await exitedWithin(done, stopGraceMs))
    return {closed: true, termination: 'closed-after-sigterm', sigtermSent};
  const sigkillSent = child.kill('SIGKILL');
  const closed = await exitedWithin(done, killGraceMs);
  return {closed, termination: 'forced', sigtermSent, sigkillSent};
}

module.exports = {stopChildAtDeadline, ACQUISITION_TIMEOUT_MS};
