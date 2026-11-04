// The session manager alone signals its owner. Module teardown is authorized
// either by its callback or by an explicit observation that the owner closed.
// Timers never replay session stop; they finish desktop teardown after close.
function createDesktopShutdown({ manager, closeModules, closeWindow, removeState,
  onDeferred = () => {}, onError = () => {}, schedule = setTimeout, cancel = clearTimeout,
  retryMs = 250 }) {
  let pending = null, stopAttempted = false, moduleCloseAttempted = false,
    retained = false, stopUncertain = false, windowClosed = false, stateRemoved = false,
    deferredTimer = null, deferredReported = false, ownershipError = null;
  const aggregate = errors => new AggregateError(errors,
    'Desktop shutdown incomplete: ' + errors.map(error => error.message).join('; '));
  async function closeModulesOnce(errors) {
    if (moduleCloseAttempted) return;
    moduleCloseAttempted = true;
    try { await closeModules(); } catch (error) { errors.push(error); }
  }
  function defer() {
    if (!deferredReported) {
      deferredReported = true;
      onDeferred();
    }
    if (!deferredTimer) {
      deferredTimer = schedule(() => {
        deferredTimer = null;
        return finish().catch(onError);
      }, retryMs);
      deferredTimer?.unref?.();
    }
  }
  async function run() {
    const errors = [];
    if (!stopAttempted) {
      // Neither an uncertain manager stop nor its authorized module shutdown
      // may be repeated by another signal, retry, or ownership-observer timer.
      stopAttempted = true;
      try {
        const result = await manager.stop({ beforeSignal: () => closeModulesOnce(errors) });
        retained = result?.retained === true;
      } catch (error) {
        stopUncertain = true;
        errors.push(error);
      }
    }
    let owns;
    try {
      owns = typeof manager.ownsListener === 'function' ? manager.ownsListener() : undefined;
      ownershipError = null;
    } catch (error) {
      owns = true;
      if (ownershipError !== error.message) errors.push(error);
      ownershipError = error.message;
    }
    if (owns === true || (owns !== false && (retained || stopUncertain))) {
      defer();
      if (errors.length) throw aggregate(errors);
      return { retained: true, closed: false };
    }
    if (deferredTimer) {
      cancel(deferredTimer);
      deferredTimer = null;
    }
    // A protected stop intentionally skipped beforeSignal while its process
    // lived. Actual observed close now permits its desktop workers to finish.
    // Missing/uncertain ownership never grants this authority.
    if (owns === false) await closeModulesOnce(errors);
    if (!windowClosed) {
      try { await closeWindow(); windowClosed = true; } catch (error) { errors.push(error); }
    }
    // Keep ownership metadata while HTTP is still live, so a later launch can
    // identify this panel and retry its shutdown instead of finding a stale port.
    if (windowClosed && !stateRemoved) {
      try { await removeState(); stateRemoved = true; } catch (error) { errors.push(error); }
    }
    if (errors.length) throw aggregate(errors);
    return { closed: windowClosed && stateRemoved };
  }
  function finish() {
    if (!pending) pending = Promise.resolve().then(run).finally(() => { pending = null; });
    return pending;
  }
  return { finish };
}

module.exports = { createDesktopShutdown };
