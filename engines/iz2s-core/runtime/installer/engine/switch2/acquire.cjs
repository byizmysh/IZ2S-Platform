const fs = require('node:fs');
const path = require('node:path');
const readline = require('node:readline');
const { spawn } = require('node:child_process');
const { a } = require('../shared/rpc.cjs');
const { request } = require('./tools/endpoint.cjs');
const { verifyProof } = require('./memory/verify-proof.cjs');
const { save, read } = require('./ro/runtime.cjs');
const {stopChildAtDeadline, ACQUISITION_TIMEOUT_MS} = require('./acquisition-stop.cjs');
const {readLiveReport} = require('./memory/live-report-read.cjs');
const {qualifyCleanCandidateRetry} = require('./memory/clean-acquisition-retry.cjs');
const delay = ms => new Promise(resolve => setTimeout(resolve, ms));

// The acquisition owns native allocations until retire() completes.
async function start() {
  // Only the exact clean, pre-cache candidate-cost miss may retry. All other
  // failures consume the application session, including deadline stops.
  const MAX_ATTEMPTS = 3;
  const overallDeadline = Date.now() + ACQUISITION_TIMEOUT_MS;
  const attemptRows = [];
  let lastError;
  for (let attempt = 0; attempt < MAX_ATTEMPTS; ++attempt) {
    const logFile = path.join(__dirname,
      attempt === 0 ? 'acquisition.log' : 'acquisition-' + (attempt + 1) + '.log');

    const log = fs.createWriteStream(logFile, { flags: 'wx', mode: 0o600 });
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
    let deadlineHandled = false;
    try {
      const deadline = overallDeadline;
      let report, recorded = false;
      for (; ;) {
        if (reportFile) {
          report = await readLiveReport(reportFile, read);
          if (!recorded) {
            // Keep the scratch owner discoverable even if acquisition later fails.
            await save(path.join(__dirname, 'current-acquisition.json'), { session: a.session, report: reportFile });
            recorded = true;
          }
        }
        if (report?.memoryControl?.ready) break;
        if (exited || spawnError)
          throw Error('FS acquisition did not complete. Inspect ' + logFile + '; restart the console before another launch.');
        if (Date.now() >= deadline) {
          deadlineHandled = true;
          const stop = await stopChildAtDeadline(child, done);
          let finalReport = null, reportReadError = null;
          if (stop.closed && reportFile) {
            try { finalReport = await read(reportFile); }
            catch (error) { reportReadError = error.message; }
          }
          const ownedCleanupReported = Boolean(stop.termination === 'closed-after-sigterm' &&
            finalReport?.cleanedUp === true && finalReport.control?.mode === 'closed' &&
            finalReport.finished &&
            !finalReport.cleanupError && !finalReport.placementUncertain &&
            finalReport.retained?.base === 0 && finalReport.retained?.handles?.length === 0 &&
            finalReport.retained?.threads?.length === 0 &&
            finalReport.retained?.created?.length === 0 &&
            finalReport.retained?.cacheNeedsRestore === false);
          await save(path.join(__dirname, 'acquisition-supervisor.json'), {
            stage: 'deadline-expired', timeoutMs: ACQUISITION_TIMEOUT_MS,
            stop, report: reportFile || null, reportReadError,
            ownedCleanupReported, consoleRestartRequired: true,
          });
          throw Error('FS acquisition exceeded its 25-minute deadline; ' +
            (ownedCleanupReported ? 'the probe reported owned cleanup' : 'owned cleanup is unverified') +
            '. Inspect ' + logFile + '; restart the console before another launch.');
        }
        await delay(500);
      }
      if (report.session !== a.session || report.error || report.cleanupError)
        throw Error('FS acquisition has an error or belongs to another session');
      const proof = await verifyProof(reportFile, a.session);
      const supervisorReport = path.join(__dirname, 'acquisition-supervisor.json');
      await save(supervisorReport, { stage: 'memory-ready',
        attempts: [...attemptRows, {report: reportFile, log: logFile, exit: null}] });
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
      if (!deadlineHandled) await abandon();
      lastError = error;
      let retry = {eligible: false, reasons: ['no-final-report']};
      if (!deadlineHandled && !spawnError && exited && reportFile) {
        try {
          const final = await read(reportFile);
          retry = qualifyCleanCandidateRetry(final, a.session, exited);
        } catch (readError) {
          retry = {eligible: false, reasons: ['final-report-read: ' + readError.message]};
        }
      }
      attemptRows.push({report: reportFile || null, log: logFile, exit: exited || null,
        retry, error: error.message});
      if (attempt === MAX_ATTEMPTS - 1 || !retry.eligible ||
          Date.now() + 1000 >= overallDeadline) break;
      await save(path.join(__dirname, 'acquisition-supervisor.json'),
        {stage: 'guarded-retry', attempts: attemptRows, maxAttempts: MAX_ATTEMPTS});
      await delay(1000);
    }
  }
  throw lastError;
}

module.exports = { start };
