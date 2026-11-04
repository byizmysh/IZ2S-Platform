// Host lifecycle policy only. No reconnect, command replay or console writes.
const fs = require('node:fs/promises');
const path = require('node:path');
const { saveJson, redact } = require('./ledger.cjs');
const codes = new Set(['ECONNRESET', 'ECONNREFUSED', 'EPIPE', 'ETIMEDOUT', 'ENOTCONN', 'ERR_STREAM_DESTROYED', 'ERR_SOCKET_CLOSED']);
function isTransportLoss(error) {
  if (!error) return false;
  return codes.has(error.code) || /^(?:Native request timed out; it was not replayed|Console disconnected|Console response timed out; restart the application|(?:Console |Native |RPC )?(?:connection|socket|link) (?:is |was )?(?:closed|ended|lost)|socket hang up|read ECONNRESET|connect ECONNREFUSED)/i.test(error.message || '') || (error.cause && isTransportLoss(error.cause));
}
async function connectionClosed(directory) {
  try { return JSON.parse(await fs.readFile(path.join(directory, 'connection-state.json'), 'utf8')).state === 'closed'; }
  catch { return false; }
}
async function canRetainRender({ error, workerRunning, servicing, stopping, directory }) {
  if (stopping || !workerRunning || servicing) return false;
  return Boolean(isTransportLoss(error) || (/connect|socket|closed|timeout|timed out|bridge/i.test(error?.message || '') && await connectionClosed(directory)));
}
async function retain(directory, reason, details = {}) {
  const row = { ...details, state: 'offline', at: new Date().toISOString(), reason: redact(reason || 'Console connection closed'),
    resourcesRetained: true, consoleExecution: 'unverified after disconnect', automaticReconnect: false, menuReturnRequiresHost: true };
  await saveJson(path.join(directory, 'offline-state.json'), row);
  return row;
}
async function waitForStop(stopping, pause = ms => new Promise(resolve => setTimeout(resolve, ms))) {
  while (!await stopping()) await pause(250);
}
async function awaitCompletion({ hostDone, shellDone, directory, isStopping, park = waitForStop, onOffline = () => {} }) {
  const outcome = await Promise.race([
    hostDone.then(() => ({ source: 'host' }), error => ({ source: 'host', error })),
    shellDone.then(() => ({ source: 'shell' }), error => ({ source: 'shell', error })),
  ]);
  if (outcome.source === 'host' && !isStopping() && await connectionClosed(directory)) {
    await retain(directory, outcome.error?.message || 'Console connection closed', { owner: 'session', endedService: outcome.source });
    await onOffline();
    await Promise.race([
      park(isStopping),
      shellDone.then(() => {
        if (!isStopping()) throw Error('Graphics service ended while the session was offline');
      }, error => { if (!isStopping()) throw error; }),
    ]);
    return;
  }
  if (outcome.error) throw outcome.error;
}
module.exports = { isTransportLoss, connectionClosed, canRetainRender, retain, waitForStop, awaitCompletion };
