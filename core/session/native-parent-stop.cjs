'use strict';
// Outer process retention only. This does not grant a native lease, reclaim any
// console resource, or prove that an application has completed native cleanup.
function create({ enabled = false, deadlineMs = 5000, onStatus = () => {} } = {}) {
  if (typeof enabled !== 'boolean' || !Number.isSafeInteger(deadlineMs) || deadlineMs < 1)
    throw TypeError('Invalid native parent stop configuration');
  const records = new Map();
  let closing = false, finished = false, keepAlive;
  function snapshot() {
    return {
      format: 'iz2s-native-parent-stop-v1', enabled, closing, finished,
      nativeLease: false,
      retained: enabled && closing && [...records.values()].some(r => !r.closed),
      children: [...records.values()].map(r => ({
        script: r.script, pid: Number.isSafeInteger(r.child.pid) ? r.child.pid : null,
        spawned: r.spawned, stopRequested: r.stopRequested,
        termAttempted: r.termAttempted, termAccepted: r.termAccepted,
        deadlineExceeded: r.deadlineExceeded, errorCode: r.errorCode,
        closed: r.closed, exitCode: r.exitCode, signal: r.signal,
      })),
    };
  }
  function publish() {
    try { onStatus(snapshot()); } catch (_) { /* Reporting must not drop owners. */ }
  }
  function begin() {
    if (!enabled || finished) return;
    const first = !closing;
    closing = true;
    // Child handles normally keep Node alive, but retention must not depend on
    // a pipe/IPC channel staying referenced. Never disconnect the parent's IPC.
    if (!keepAlive) keepAlive = setInterval(() => {}, 1000);
    if (first) publish();
  }
  function signalOnce(r) {
    if (!r.spawned || r.closed || !r.stopRequested || r.termAttempted) return;
    r.termAttempted = true; // Before kill: false/throw is unknown, never a retry.
    try {
      r.termAccepted = r.child.kill('SIGTERM') === true;
      if (!r.termAccepted) r.errorCode ||= 'SIGTERM_NOT_ACCEPTED';
    } catch (error) {
      r.errorCode = typeof error?.code === 'string' ? error.code : 'SIGTERM_THROW';
    }
    publish();
  }
  // Call synchronously immediately after spawn returns. Only actual close is a
  // completion acknowledgement; error/exit, kill(false), and deadlines are not.
  function track(child, { script = '' } = {}) {
    if (!enabled) return;
    if (!child || typeof child.once !== 'function' || typeof child.kill !== 'function')
      throw TypeError('Expected an owned ChildProcess');
    if (records.has(child)) return;
    let resolve;
    const r = { child, script: String(script), spawned: false, closed: false,
      stopRequested: false, termAttempted: false, termAccepted: false,
      deadlineExceeded: false, errorCode: null, exitCode: null, signal: null,
      timer: null, done: new Promise(done => { resolve = done; }) };
    records.set(child, r);
    child.once('spawn', () => { r.spawned = true; signalOnce(r); publish(); });
    child.on('error', error => {
      r.errorCode = typeof error?.code === 'string' ? error.code : 'CHILD_ERROR';
      publish();
    });
    child.once('close', (code, signal) => {
      r.closed = true;
      r.exitCode = code;
      r.signal = signal;
      if (r.timer) clearTimeout(r.timer);
      r.timer = null;
      resolve({ code, signal });
      publish();
    });
    // The runner prevents new launches once STOP is latched. Retain unexpected
    // late children as well; never throw after their spawn and lose the handle.
    if (closing) { finished = false; stopChild(child); }
    publish();
  }
  function stopChild(child) {
    if (!enabled) throw Error('Native parent retention is disabled');
    const r = records.get(child);
    if (!r) throw Error('Untracked child; refusing signal');
    if (r.closed || r.stopRequested) return r.done;
    begin();
    r.stopRequested = true;
    r.timer = setTimeout(() => {
      if (r.closed) return;
      r.deadlineExceeded = true;
      publish(); // Intentionally no resolution, rejection, signal, or retry.
    }, deadlineMs);
    signalOnce(r);
    publish();
    return r.done;
  }
  async function waitAllClosed() {
    if (!enabled) return;
    // The owning runner closes its launch gate before beginning shutdown. Loop
    // also accounts for an unexpected child tracked while a prior batch closes.
    while ([...records.values()].some(r => !r.closed))
      await Promise.all([...records.values()].filter(r => !r.closed).map(r => r.done));
  }
  function finish() {
    if (!enabled) return;
    if ([...records.values()].some(r => !r.closed))
      throw Error('Cannot finish while child exit is unconfirmed');
    finished = true;
    if (keepAlive) clearInterval(keepAlive);
    keepAlive = null;
    publish();
  }
  return Object.freeze({ track, begin, stopChild, waitAllClosed, finish, snapshot });
}
module.exports = { create };
