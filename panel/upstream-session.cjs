const fs = require('node:fs/promises'),
  path = require('node:path'),
  crypto = require('node:crypto'),
  { spawn } = require('node:child_process');
const {
  createLedger,
  readJson,
  saveJson,
  redact,
} = require('../core/session/ledger.cjs');
const UUID = /^[a-f0-9-]{36}$/;
function create({
  root,
  settings,
  node,
  arm,
  devices,
  connections,
  preview = false,
  environment = {},
  selection,
}) {
  const ledger = createLedger(root),
    recoveryFile = path.join(root, 'data/state/upstream-input-recovery.json');
  let current = null,
    last = null,
    child = null,
    probe = {
      ready: false,
      items: ['server', 'arm', 'usb', 'lan'].map((id) => ({
        id,
        ready: false,
        detail: 'Not checked',
      })),
    },
    checking = null,
    notice = null,
    busy = false,
    cancellation = null,
    inputRecovery = null,
    queue = Promise.resolve();
  const recoveryReady = readJson(recoveryFile).then((value) => {
    inputRecovery = value;
  });
  async function bootstrapInfo(loader = selection(), plan) {
    plan ??= loader.sourcePlan();
    if (!plan.bootstrap)
      return {
        id: loader.manifest?.id || plan.loaderId,
        name: null,
        sourceSlot: 0,
        sha256: null,
        preloaded: true,
        setupAvailable: false,
        lines: 0,
        ready: true,
      };
    const bytes = await fs.readFile(plan.bootstrap.file),
      id = loader.manifest?.id || plan.loaderId,
      sha256 = crypto.createHash('sha256').update(bytes).digest('hex'),
      file = path.join(
        root,
        'data/state/loaders',
        id,
        'original-bootstrap.json',
      ),
      saved = await readJson(file);
    return {
      file,
      id,
      name: plan.bootstrap.saveName,
      sourceSlot: plan.bootstrap.sourceSlot ?? 0,
      sha256,
      preloaded: plan.startup?.preloaded === true,
      setupAvailable: plan.startup?.sourceSetup !== false,
      lines: bytes
        .toString('ascii')
        .replace(/\r\n/g, '\n')
        .trimEnd()
        .split('\n').length,
      ready:
        saved?.name === plan.bootstrap.saveName &&
        saved?.sha256 === sha256 &&
        saved?.userConfirmed === true,
    };
  }
  async function confirmBootstrap(info, method) {
    await saveJson(info.file, {
      loaderId: info.id,
      name: info.name,
      sha256: info.sha256,
      lines: info.lines,
      sourceSlot: info.sourceSlot,
      userConfirmed: true,
      method,
      confirmedAt: new Date().toISOString(),
      consoleReadback: false,
    });
  }

  const log = (type, text) =>
    current ? ledger.append(current.id, type, text) : Promise.resolve();
  const persist = () => (current ? ledger.write(current) : Promise.resolve());
  async function refresh() {
    if (preview || current) return probe;
    if (checking) return checking;
    checking = devices
      .inspect()
      .then((value) => (probe = value))
      .finally(() => (checking = null));
    return checking;
  }
  async function fail(error, row = current) {
    if (!row || row.status === 'failed') return;
    if (current === row) {
      terminate(true);
      child = null;
      if (row.inputJob) cancelInput(row).catch(() => {});
    }
    row.status = 'failed';
    const diagnostic = await require('../core/session/failure-report.cjs').inspect(row.runDirectory, error.message, {directFs:true});
    row.error = redact(diagnostic.message);
    if (diagnostic.stage) row.failedStage = diagnostic.stage;
    if (diagnostic.report) row.failureReport = diagnostic.report;
    if (diagnostic.resourcesRetained) row.resourcesRetained = true;
    row.completed = new Date().toISOString();
    row.needsRestart = true;
    await ledger.append(row.id, 'error', row.error);
    await ledger.write(row);
    if (current === row) current = null;
    last = row;
    notice = { type: 'error', message: row.error };
  }
  async function input(plan, row) {
    row.inputJob = plan.id;
    row.inputActive = true;
    await persist();
    try {
      await arm.performPlan(plan, (event) => {
        if (
          event.detail === 'Entered source line' &&
          Number.isInteger(event.fields?.line) &&
          Number.isInteger(event.fields?.total)
        ) {
          row.progress = { line: event.fields.line, total: event.fields.total };
          ledger.write(row).catch(() => {});
        }
        if (event.detail)
          ledger.append(row.id, 'input', event.detail).catch(() => {});
      });
    } finally {
      row.inputActive = false;
      await ledger.write(row);
    }
  }
  function matches(patterns, line) {
    return (patterns || []).some((p) =>
      (p instanceof RegExp ? p : new RegExp(p)).test(line),
    );
  }
  async function run(row, loader, plan) {
    if (current !== row || row.status === 'stopping') return;
    const command = plan.command || node;
    child = spawn(command, plan.args || [], {
      cwd: plan.cwd || root,
      env: {
        ...process.env,
        ...environment,
        NODE: node,
        PATH:
          path.join(root, 'tools') + path.delimiter + (process.env.PATH || ''),
        ...plan.env,
        HB_PORT: String(settings.consolePort),
        IZ2S_ATTEMPT_ID: row.id,
      },
      detached: process.platform === 'linux',
      stdio: ['ignore', 'pipe', 'pipe'],
    });
    const owned = child;
    let pending = '',
      stderr = '',
      sent = false,
      completed = false;
    async function consume(line) {
      if (
        current !== row ||
        ['stopping', 'cancelled', 'failed'].includes(row.status)
      )
        return;
      const folder = line.match(/^Run folder: ([^\r\n]+)$/);
      const peer = line.match(/^Console peer: ([0-9.]+)$/);
      if (peer && require('node:net').isIPv4(peer[1])) row.consoleAddress = peer[1];
      if (folder && !sent) {
        const directory = path.resolve(folder[1]),
          roots = [
            plan.runRoot,
            path.join(plan.cwd || root, 'data/sessions'),
            path.join(root, 'data/sessions'),
          ]
            .filter(Boolean)
            .map((p) => path.resolve(p));
        if (!roots.some((base) => path.dirname(directory) === base))
          throw Error('The loader returned an unexpected run folder');
        sent = true;
        row.runDirectory = directory;
        await persist();
        const connect = (
          await fs.readFile(path.join(directory, 'connect.txt'), 'utf8')
        ).trim();
        if (
          !connect ||
          connect.includes('\n') ||
          connect.includes('\r') ||
          connect.length > 2048
        )
          throw Error('The loader returned an invalid connection command');
        const words = connect.match(/^V=START\(([0-9,]+)\)$/)?.[1].split(',');
        if (
          !words ||
          words.length !== plan.bootstrap.arity ||
          Number(words[0]) !== settings.consolePort ||
          words.slice(1, 5).join('.') !== settings.consoleHost
        )
          throw Error(
            'The loader connection command does not match its selected bootstrap, address and port',
          );
        const steps = loader.connectSteps
          ? await loader.connectSteps({
              command: connect,
              bootstrapName: plan.bootstrap.saveName,
              preloaded: plan.startup?.preloaded === true,
            })
          : plan.connectSteps?.map((s) =>
              s.type === 'line' && s.text === '@CONNECT'
                ? { ...s, text: connect }
                : s,
            );
        if (!steps?.length)
          throw Error('This loader has no keyboard launch sequence');
        await input({ id: row.id, steps }, row);
      }
      if (matches(plan.lifecycle?.readyPatterns, line)) {
        row.status = 'running';
        row.connectedAt = new Date().toISOString();
        await persist();
        await log('ready', 'The selected upstream loader is running');
        if (!row.filesPageRequested) {
          row.filesPageRequested = true;
          void require('../core/rpc-browser.cjs').onSessionReady(root, {...row}, {preview})
            .catch(error => ledger.append(row.id, 'desktop', 'Files page: ' + error.message).catch(() => {}));
        }
      }
      if (matches(plan.lifecycle?.completedPatterns, line)) {
        completed = true;
        row.result = 'completed';
        await persist();
      }
      if (
        /ready|starting|verified|execution|running|waiting|connected|checking/i.test(
          line,
        )
      )
        await log('stage', line);
    }
    owned.stdout.on('data', (bytes) => {
      pending += bytes;
      let at;
      while ((at = pending.indexOf('\n')) >= 0) {
        const line = pending.slice(0, at);
        pending = pending.slice(at + 1);
        queue = queue.then(() => consume(line)).catch((e) => fail(e, row));
      }
    });
    owned.stderr.on('data', (bytes) => {
      stderr = (stderr + bytes).slice(-4000);
    });
    owned.once('error', (e) => fail(e, row).catch(() => {}));
    owned.once('close', (code, signal) => {
      queue = queue
        .then(async () => {
          if (child === owned) child = null;
          if (current !== row) return;
          if (row.status === 'stopping') {
            row.status = 'stopped';
            row.needsRestart = true;
          } else if (
            code === 0 &&
            (completed || plan.lifecycle?.exitZeroCompletes)
          ) {
            row.status = 'complete';
            notice = {
              type: 'success',
              message: 'The upstream program finished.',
            };
          } else
            return fail(
              Error(
                redact(
                  stderr.trim().split('\n').find(line => /^(?:[\w.]+)?Error:/.test(line.trim())) ||
                    stderr.trim().split('\n')[0] ||
                    'Loader stopped (' + (signal || code) + ')',
                ),
              ),
              row,
            );
          row.completed = new Date().toISOString();
          await ledger.write(row);
          last = row;
          current = null;
        })
        .catch((e) => fail(e, row));
    });
  }
  async function begin({
    kind = 'launch',
    requestId,
    screenReady,
    sourceReady = false,
  }) {
    await recoveryReady;
    if (preview) throw Error('Preview does not send commands to devices');
    if (current || busy || child || inputRecovery)
      throw Error('Stop the current session before starting');
    if (!UUID.test(requestId || '')) throw Error('Invalid request identifier');
    if (!['launch', 'bootstrap'].includes(kind))
      throw Error('Unknown launch step');
    if (!screenReady) throw Error('Confirm the current SmileBASIC screen');
    busy = true;
    try {
      const prior = await ledger.read(requestId);
      if (prior) return { attempt: prior, duplicate: true };
      const loader = selection(),
        plan =
          kind === 'bootstrap'
            ? loader.sourcePlan()
            : await loader.plan({
                host: settings.consoleHost,
                node,
                input: 'manual',
                port: settings.consolePort,
                runtime: 'original',
              });
      const source = await bootstrapInfo(loader, plan);
      if (kind === 'bootstrap' && !source.setupAvailable)
        throw Error(
          'This loader connects to an existing bootstrap; source entry is not part of this launch.',
        );
      if (kind === 'launch' && !source.preloaded && !source.ready) {
        if (sourceReady !== true)
          throw Error(
            'First install and save ' +
              source.name +
              ' using First setup, or explicitly confirm that this loader’s source is already saved.',
          );
        await confirmBootstrap(source, 'existing-program-confirmation');
      }
      const checks = await refresh();
      if (!checks.ready)
        throw Error('Complete the connection settings before starting');
      current = {
        id: requestId,
        kind,
        loaderId: loader.manifest?.id || plan.loaderId,
        upstream: true,
        consoleAddress: settings.consoleAddress,
        status: 'starting',
        created: new Date().toISOString(),
        selectedModules: [],
        setup:
          kind === 'bootstrap'
            ? {
                stage: 'typing',
                name: plan.bootstrap.saveName,
                sourceSha256: source.sha256,
                sourceLines: source.lines,
                sourceSlot: source.sourceSlot,
              }
            : undefined,
      };
      notice = null;
      await persist();
      const row = current;
      if (kind === 'bootstrap') {
        (async () => {
          try {
            await input(
              {
                id: row.id,
                steps: plan.bootstrap.editorSteps || [
                  {
                    type: 'line',
                    text: 'LIST ' + (plan.bootstrap.sourceSlot ?? 0) + ':1',
                    delayMs: 2000,
                  },
                ],
                sourceFile: plan.bootstrap.file,
              },
              row,
            );
            if (current !== row) return;
            row.status = 'review';
            row.setup.stage = 'review';
            await persist();
            await log(
              'ready',
              'Source entry finished. Review it on the console, then save.',
            );
          } catch (e) {
            await fail(e, row);
          }
        })();
      } else {
        await log(
          'stage',
          'Starting ' +
            (loader.manifest?.name || source.id) +
            ' from its own server: ' +
            plan.cwd,
        );
        await log(
          'stage',
          source.preloaded
            ? 'Using bootstrap already loaded in slot 0; source entry and LOAD are skipped.'
            : 'Loading saved bootstrap ' + source.name,
        );
        run(row, loader, plan).catch((e) => fail(e, row));
      }
      return { attempt: row };
    } finally {
      busy = false;
    }
  }
  async function setupAction({ action, requestId, savedConfirmed = false }) {
    const row = current;
    if (!row || row.kind !== 'bootstrap')
      throw Error('No upstream source installation is active');
    if (row.inputActive) throw Error('Wait until source entry finishes');
    if (!UUID.test(requestId || '')) throw Error('Invalid request identifier');
    if (action === 'save' && row.setup.stage === 'review') {
      row.setup.stage = 'saving';
      await persist();
      (async () => {
        try {
          await input(
            {
              id: requestId,
              steps: [
                { type: 'key', key: 'f4', delayMs: 350 },
                {
                  type: 'line',
                  text: require('../core/session/install-slot.cjs').commands(
                    row.setup.name,
                    row.setup.sourceSlot,
                  ).save,
                  delayMs: 500,
                },
              ],
            },
            row,
          );
          if (current === row) {
            row.setup.stage = 'save_confirmation';
            row.status = 'review';
            await persist();
          }
        } catch (e) {
          await fail(e, row);
        }
      })();
    } else if (
      action === 'saved' &&
      row.setup.stage === 'save_confirmation' &&
      savedConfirmed
    ) {
      const info = await bootstrapInfo();
      if (
        info.sha256 !== row.setup.sourceSha256 ||
        info.name !== row.setup.name ||
        info.sourceSlot !== row.setup.sourceSlot
      )
        throw Error(
          'The selected source changed; install it again before confirming.',
        );
      await confirmBootstrap(info, 'source-entry-and-save-confirmation');
      row.status = 'complete';
      row.completed = new Date().toISOString();
      row.needsRestart = true;
      await ledger.write(row);
      last = row;
      current = null;
      notice = {
        type: 'success',
        message:
          'Source saved. Fully restart Switch and open SmileBASIC Direct before Launch.',
      };
    } else throw Error('This setup step is unavailable');
    return { ok: true };
  }
  function terminate(force = false) {
    if (!child) return;
    try {
      if (
        process.platform === 'linux' &&
        Number.isInteger(child.pid) &&
        child.pid > 1
      )
        process.kill(-child.pid, force ? 'SIGKILL' : 'SIGTERM');
      else child.kill(force ? 'SIGKILL' : 'SIGTERM');
    } catch (e) {
      if (e.code !== 'ESRCH') throw e;
    }
  }
  async function cancelInput(row) {
    if (cancellation) return cancellation;
    inputRecovery = { id: row.id, inputJob: row.inputJob || row.id };
    await saveJson(recoveryFile, inputRecovery);
    cancellation = arm
      .halt(inputRecovery.inputJob)
      .then(
        async () => {
          inputRecovery = null;
          await fs.rm(recoveryFile, { force: true });
        },
        (error) => {
          notice = {
            type: 'error',
            message: 'Input cancellation needs confirmation: ' + error.message,
          };
        },
      )
      .finally(() => (cancellation = null));
    return cancellation;
  }
  async function stop() {
    const row = current;
    if (!row) return;
    row.status = 'stopping';
    await persist();
    await cancelInput(row);
    terminate();
    if (!child) {
      row.status = 'cancelled';
      row.needsRestart = !!row.inputJob;
      row.completed = new Date().toISOString();
      await ledger.write(row);
      last = row;
      current = null;
    }
  }
  async function hardStop() {
    await recoveryReady;
    const row = current || inputRecovery;
    terminate(true);
    child = null;
    if (current) {
      current.status = 'cancelled';
      current.needsRestart = true;
      current.completed = new Date().toISOString();
      await ledger.write(current);
      last = current;
      current = null;
    }
    if (row) cancelInput(row).catch(() => {});
    notice = {
      type: 'error',
      message: 'Host stopped. Restart the console manually.',
    };
    return { ok: true };
  }
  async function snapshot() {
    await recoveryReady;
    const row = current || last,
      log = row ? await ledger.recent(row.id) : [],
      source = await bootstrapInfo();
    return {
      sourcePreparation: {
        name: source.name,
        sourceSlot: source.sourceSlot,
        lines: source.lines,
        ready: source.ready,
        required: !source.preloaded,
        preloaded: source.preloaded,
      },
      version: 21,
      preview,
      setupWizard: source.setupAvailable,
      hardStopAvailable: true,
      recoverySetupAvailable: false,
      bootstrapProfile: { recovery: false },
      recoverySource: null,
      link: null,
      consoleState: null,
      emergencyInputPending: !!inputRecovery,
      checks: probe,
      active: current,
      lastAttempt: current ? null : last,
      notification: notice,
      history: await ledger.listFailures(),
      log,
      library: [],
      jobs: [],
      installed: [],
      hostModules: {},
      connected: current?.status === 'running',
      streamUrl: null,
      connections: {
        ...connections.snapshot(),
        locked: !!current || !!inputRecovery,
      },
      settings: {
        consoleHost: settings.consoleHost,
        consoleAddress: settings.consoleAddress,
      },
    };
  }
  return {
    refresh,
    begin,
    setupAction,
    stop,
    hardStop,
    snapshot,
    ownsListener: () => !!child,
    configureConnection: async (body) => {
      if (current || busy || inputRecovery)
        throw Error('Stop the session first');
      await connections.update(body);
      await refresh();
      return { ok: true };
    },
    retry: async () => {
      if (current || child || inputRecovery)
        throw Error('Stop the session first');
      last = null;
      notice = null;
      return { ok: true };
    },
    setupReport: async (id) => JSON.stringify(await ledger.read(id), null, 2),
    remove: async (id) => {
      if (id === current?.id)
        throw Error('An active attempt cannot be removed');
      const row = await ledger.read(id);
      if (!row || !['failed', 'cancelled'].includes(row.status))
        throw Error('Only failed or cancelled attempts can be removed');
      if (row.runDirectory) {
        const directory = path.resolve(row.runDirectory),
          allowed = require('../engines/index.cjs').create(root).list().flatMap((loader) => [
            path.join(loader.folder, 'data/sessions'),
            path.join(loader.folder, 'upstream/runs'),
          ]);
        if (
          ![...allowed, path.join(root, 'data/sessions')].includes(
            path.dirname(directory),
          ) ||
          !/^(switch2|launcher)-[A-Za-z0-9]+$/.test(path.basename(directory))
        )
          throw Error('Unexpected upstream run directory');
        await fs.rm(directory, { recursive: true, force: true });
        delete row.runDirectory;
        await ledger.write(row);
      }
      await ledger.removeFailure(id, current?.id);
      if (last?.id === id) last = null;
    },
    historyLog: (id) => ledger.all(id),
    dismiss: () => (notice = null),
  };
}
module.exports = { create };
