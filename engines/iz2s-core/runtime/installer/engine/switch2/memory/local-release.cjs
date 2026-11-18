'use strict';

// The trigger worker wakes on a separate arm gate, then releases the existing
// thirteen-worker master gate on the console. No new native thread is created.
const DELAYS_MS = Object.freeze([40]);
const HOST_QUIET_MS = 120;
const QUIET_MARGIN_MS = 2;

function delayFromEnv(env = process.env) {
  const value = env.LOCAL_RELEASE_DELAY_MS;
  // The shared Wi-Fi candidate always uses 40 ms unless an explicit,
  // supported selection is supplied. Missing shell configuration is normal.
  if (value === undefined) return 40;
  if (value !== '40')
    throw Error('LOCAL_RELEASE_DELAY_MS must be exactly 40');
  return Number(value);
}

function triggerPrefix(delayMs, arm, master, {wait, sleep, tick, signal}) {
  if (!DELAYS_MS.includes(delayMs)) throw Error('Unreviewed local release delay');
  return [wait(arm), tick('localArmWake'), sleep(delayMs * 1000000),
    tick('localSignalStart'), signal(master), tick('localSignalEnd')];
}

async function armAndQuiet({signalArm, requests, now, pause, quietMs = HOST_QUIET_MS}) {
  if (quietMs !== HOST_QUIET_MS) throw Error('Unreviewed host quiet interval');
  const requestCountBeforeArm = requests();
  const armStarted = now();
  await signalArm();
  const armReturned = now();
  const requestCountAfterArm = requests();
  await pause(quietMs);
  const quietEnded = now();
  const requestCountBeforeJoin = requests();
  return {requestCountBeforeArm, requestCountAfterArm, requestCountBeforeJoin,
    armRttMs: armReturned - armStarted, hostQuietMs: quietEnded - armReturned,
    probeRpcDuringQuiet: requestCountBeforeJoin - requestCountAfterArm};
}

function qualifyTiming({delayMs, armRttMs, hostQuietMs, probeRpcDuringQuiet,
  nativeArmToSignalMs, nativeArmToWaveEndMs}) {
  return {
    preReleaseQuiet: delayMs > 0 &&
      nativeArmToSignalMs >= delayMs - QUIET_MARGIN_MS &&
      nativeArmToSignalMs > armRttMs + QUIET_MARGIN_MS,
    hostPollingAfterWave: probeRpcDuringQuiet === 0 &&
      hostQuietMs > nativeArmToWaveEndMs + QUIET_MARGIN_MS,
  };
}

module.exports = {DELAYS_MS, HOST_QUIET_MS, QUIET_MARGIN_MS,
  delayFromEnv, triggerPrefix, armAndQuiet, qualifyTiming};
