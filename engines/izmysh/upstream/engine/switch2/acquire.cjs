const fs = require('node:fs');
const path = require('node:path');
const readline = require('node:readline');
const { spawn } = require('node:child_process');
const { a } = require('../shared/rpc.cjs');
const { request } = require('./tools/endpoint.cjs');
const { verifyProof } = require('./memory/verify-proof.cjs');
const { save, read } = require('./ro/runtime.cjs');
const delay = ms => new Promise(resolve => setTimeout(resolve, ms));

// A failed status is not a release proof. Killing the host probe does not
// join its console threads or restore a borrowed cache.
function classifyFailure({report, session, exited, deadlineExpired, error, spawnError}) {
  const retained = report?.retained;
  const ownershipClean = report?.session === session && report.cleanedUp === true &&
    !report.cleanupError && !report.cacheRestoreError && !report.placementUncertain &&
    retained?.base === 0 && Array.isArray(retained.handles) && !retained.handles.length &&
    Array.isArray(retained.threads) && !retained.threads.length &&
    Array.isArray(retained.workers) && retained.workers.every(worker => worker?.joined === true) &&
    Array.isArray(retained.created) && !retained.created.length && !retained.group &&
    retained.cacheNeedsRestore === false && !report.memoryControl?.ready &&
    !report.memoryControl?.pending && !report.retainedMemory?.pending &&
    !report.retainedMemory?.ready && !report.retainedMemory?.promoted &&
    (!report.nativeRo?.worker || report.nativeRo.worker.joined === true);
  const messages = [error?.message, report?.error, report?.cleanupError, spawnError?.message]
    .filter(Boolean).join('\n');
  let reason;
  if (deadlineExpired) reason = 'Acquisition deadline expired; no automatic retry';
  else if (/timed out|did not terminate|resources retained|connection is not ready/i.test(messages))
    reason = 'Native execution or transport did not finish reliably; no automatic retry';
  else if (!Number.isInteger(exited?.code) || exited.signal || spawnError)
    reason = 'Probe process completion is unknown or abnormal; no automatic retry';
  else if (!ownershipClean)
    reason = 'No complete release proof for native threads, handles and cache ownership; no automatic retry';
  else reason = 'Candidate rejected after confirmed cleanup';
  return {canRetry: reason === 'Candidate rejected after confirmed cleanup',
    resourcesRetained: !ownershipClean, reason};
}

// The acquisition owns native allocations until retire() completes.
async function start() {
  // Repeat only a clean candidate rejection, never an uncertain native batch.
  const MAX_ATTEMPTS = 3;
  let lastError;
  const failures = [];
  for (let attempt = 0; attempt < MAX_ATTEMPTS; ++attempt) {
    if (attempt > 0) {
      try {
        await require('./cleanup-scratch.cjs').main();
      } catch {
        // Continue if session or cleanup was transiently unavailable
      }
    }
    const logFile = path.join(__dirname, 'acquisition.log');

    if (attempt === 0) {
      try {
        await fs.promises.unlink(logFile);
      } catch (err) {
        if (err.code !== 'ENOENT') throw err;
      }
    }

    const log = fs.createWriteStream(logFile, { flags: attempt === 0 ? 'wx' : 'a', mode: 0o600 });
    await new Promise((resolve, reject) => { log.once('open', resolve); log.once('error', reject); });
    const child = spawn(process.execPath, [path.join(__dirname, 'memory/probe-reserved-gap.cjs')],
      { cwd: __dirname, stdio: ['ignore', 'pipe', 'pipe'], windowsHide: true });
    child.stdout.pipe(log, { end: false }); child.stderr.pipe(log, { end: false });
    let reportFile, exited, spawnError;
    child.once('error', error => { spawnError = error; });
    const done = new Promise(resolve => child.once('close', (code, signal) => {
      exited = { code, signal }; log.end(); resolve(exited);
    }));
    const lines = readline.createInterface({ input: child.stdout });
    lines.on('line', line => {
      let row;
      try { row = JSON.parse(line); } catch { return; }
      if (row.stage === 'connected') reportFile = row.file;
    });
    const abandon = async () => {
      if (!exited) child.kill('SIGKILL');
      await done;
    };
    let deadlineExpired = false;
    try {
      const deadline = Date.now() + 10 * 60 * 1000;
      let report, recorded = false;
      for (; ;) {
        if (reportFile) {
          try {
            report = await read(reportFile);
            if (!recorded) {
              // Keep the scratch owner discoverable even if acquisition later fails.
              await save(path.join(__dirname, 'current-acquisition.json'), { session: a.session, report: reportFile });
              recorded = true;
            }
          } catch {
            // Transient read/parse collision while report is being saved on Windows.
            // Safe to ignore and retry on the next 500ms tick.
          }
        }
        if (report?.memoryControl?.ready) break;
        deadlineExpired = Date.now() >= deadline;
        if (exited || spawnError || deadlineExpired)
          throw Error('FS acquisition did not complete. Inspect ' + logFile + '; restart the console before another launch.');
        await delay(500);
      }
      if (report.session !== a.session || report.error || report.cleanupError)
        throw Error('FS acquisition has an error or belongs to another session');
      const proof = await verifyProof(reportFile, a.session);
      const supervisorReport = path.join(__dirname, 'acquisition-supervisor.json');
      await save(supervisorReport, { stage: 'memory-ready', attempts: [{ report: reportFile }], failures });
      const current = {
        session: a.session, report: reportFile, supervisorReport,
        socketPath: report.memoryControl.socketPath, proof, finished: new Date().toISOString()
      };
      await save(path.join(__dirname, 'current-acquisition.json'), current);
      return {
        ...current, abandon,
        async retire() {
          const latest = await read(reportFile);
          const retained = latest.retainedMemory;
          if (!latest.memoryControl?.ready || latest.placementUncertain || retained?.pending ||
            latest.memoryControl.pending || latest.retained.threads.length ||
            retained?.promoted && (!retained.ready || !retained.canonical || !retained.zeroCountVerified))
            throw Error('The retained endpoint cannot be retired from its current state');
          const state = latest.nativeRo;
          if (state?.worker && !state.worker.joined) throw Error('A native worker is still pending');
          const result = await request(current.socketPath, { command: 'retire' });
          if (!result.retired && !result.retiring) throw Error('The retained endpoint did not retire');
          const deadline = Date.now() + 60000;
          while (!exited && Date.now() < deadline) await delay(100);
          if (!exited) throw Error('FS cleanup timed out; inspect ' + reportFile);
          const final = await read(reportFile);
          if (exited.code !== 0 || !final.cleanedUp || final.cleanupError)
            throw Error('FS resources did not clean up completely; inspect ' + reportFile);
          return { cleanedUp: true, report: reportFile };
        },
      };
    } catch (error) {
      if (!error || typeof error !== 'object') error = new Error(String(error));
      const report = reportFile ? await read(reportFile).catch(() => null) : null;
      const decision = classifyFailure({report, session: a.session, exited,
        deadlineExpired, error, spawnError});
      const retry = decision.canRetry && attempt < MAX_ATTEMPTS - 1;
      const failure = {attempt: attempt + 1, report: reportFile || null,
        childPid: child.pid || null, childExited: exited || null,
        socketPath: report?.memoryControl?.socketPath || null,
        ...decision, retry, error: report?.error || error.stack || String(error),
        wrapperError: error.stack || String(error), finished: new Date().toISOString()};
      failures.push(failure);
      const cause = report?.error?.split('\n')[0] || error.message || String(error);
      lastError = new Error('FS acquisition stopped after attempt ' + (attempt + 1) +
        ': ' + cause + '. ' + decision.reason + '. Inspect ' + (reportFile || logFile));
      lastError.cause = error;
      lastError.resourcesRetained = decision.resourcesRetained;
      lastError.report = reportFile || null;
      lastError.acquisition = failure;
      // Preserve a live owner and endpoint on proof/deadline failures.
      try {
        await save(path.join(__dirname, 'acquisition-supervisor.json'), {
          stage: retry ? 'retrying-clean-candidate' : 'acquisition-stopped',
          session: a.session, attempts: failures, resourcesRetained: decision.resourcesRetained,
          ...(retry ? {} : {error: lastError.message}), finished: failure.finished});
        await save(path.join(__dirname, 'current-acquisition.json'), {
          session: a.session, report: reportFile || null,
          phase: retry ? 'retrying-clean-candidate' : 'acquisition-stopped',
          childPid: failure.childPid, socketPath: failure.socketPath, failure});
      } catch (diagnosticError) {
        failure.retry = false;
        lastError.diagnosticError = diagnosticError.stack || String(diagnosticError);
        lastError.message += '; failure diagnostics could not be saved; no automatic retry';
        throw lastError;
      }
      if (!retry) break;
    }
    if (attempt < MAX_ATTEMPTS - 1) await delay(1000);
  }
  throw lastError;
}

module.exports = { start, classifyFailure };
