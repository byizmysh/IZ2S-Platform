// IZlauncher is the application layer; a loader supplies only its entry transport.
const fs = require('node:fs/promises'),
  path = require('node:path'),
  checks = require('../core/session/source-check.cjs');
const engines = require('../engines/index.cjs');
function selected({ root, id, settings = {}, host, port, recovery = false }) {
  const engines = require('../engines/index.cjs').create(root);
  if (!engines.IDS.includes(id))
    throw Error('Unknown IZlauncher loading method');
  const meta = engines.list().find((e) => e.id === id);
  if (!meta.bootstrapDescriptor) throw Error('This engine has no bootstrap');
  const resumable=meta.capabilities?.receiptRecovery===true;
  return {
    id,
    protocol: resumable?2:1,
    resumable,
    bootstrap: {
      file: path.join(meta.folder, meta.bootstrap),
      sourceSlot: meta.bootstrapDescriptor.installSlot,
      saveName: meta.bootstrapDescriptor.saveName,
      arity: meta.bootstrapDescriptor.arity,
      loaderId: id,
      recovery: meta.bootstrapDescriptor.recovery===true,
    },
  };
}
function create({
  root,
  id,
  settings = {},
  configuration,
  input,
  installSlot,
}) {
  const engines = require('../engines/index.cjs').create(root);
  const slots = require('../core/session/install-slot.cjs');
  installSlot = slots.resolve(
    installSlot,
    engines.list().find((e) => e.id === id)?.bootstrapDescriptor?.installSlot ??
      0,
  );
  const profileFile = path.join(
    root,
    'data/state/loaders',
    id,
    'bootstrap-profile.json',
  );
  const transport = (recovery) =>
    selected({
      root,
      id,
      settings,
      host: configuration.settings.consoleHost,
      port: configuration.settings.consolePort,
      recovery,
    });
  async function profile() {
    try {
      return {
        ...JSON.parse(await fs.readFile(profileFile, 'utf8')),
        recovery: false,
      };
    } catch (e) {
      if (e.code !== 'ENOENT') throw e;
    }
    return { recovery: false };
  }
  function installation(recovery = false) {
    const value = transport(recovery);
    return {
      ...value,
      bootstrap: {
        ...value.bootstrap,
        sourceSlot: installSlot,
      },
    };
  }
  async function connectionBootstrapName(recovery = false) {
    return transport(recovery).bootstrap.saveName;
  }
  async function sourcePlan(_root, recovery = false) {
    const value = installation(recovery);
    return checks.makePlan(await fs.readFile(value.bootstrap.file), {
      recovery: true,
      sourceSlot: value.bootstrap.sourceSlot ?? 0,
    });
  }
  const sourceChecks = {
    readPlan: sourcePlan,
    compare: checks.compare,
    async report(_root, row, events) {
      const value = installation(!!row.setup?.recovery),
        plan = await sourcePlan(root, !!row.setup?.recovery);
      return [
        'IZ2S Platform · source entry',
        'Loading method: ' + id,
        'Program: ' + value.bootstrap.saveName,
        'Source SHA-256: ' + plan.sha256,
        'State: ' + row.status,
        'Source lines: ' + plan.lines,
        'Received console text has not been captured.',
        '',
        ...events.map((e) => e.at + ' ' + e.message),
        '',
        await fs.readFile(value.bootstrap.file, 'utf8'),
      ].join('\n');
    },
  };
  async function preload(onEvent) {
    if (!input.stagePlans) return;
    const value = installation(false), slot = value.bootstrap.sourceSlot;
    const checks = (await sourcePlan(root, false)).commands.map(text => ({type:'line',text,delayMs:2000}));
    const save = [{type:'line',text:slots.commands(value.bootstrap.saveName,slot).save,delayMs:2000}];
    const editor = [{type:'key',key:'f4',delayMs:2000}];
    const command = 'V=START(' + [configuration.settings.consolePort,...configuration.settings.consoleHost.split('.')].join(',') + ')';
    await input.stagePlans({
      bootstrap:{steps:[{type:'line',text:'LIST '+slot+':1',delayMs:2000}],sourceFile:value.bootstrap.file},
      'verify-editor':{steps:[...editor,...checks]}, 'verify-direct':{steps:checks},
      'save-editor':{steps:[...editor,...save]}, 'save-direct':{steps:save},
      connect:{steps:require('../engines/common.cjs').connectSteps({command,bootstrapName:value.bootstrap.saveName})},
    }, onEvent);
  }
  async function perform(
    action,
    requestId,
    runDirectory,
    onEvent,
    { recovery = false } = {},
  ) {
    const value =
        action === 'connect' ? transport(recovery) : installation(recovery),
      steps = [];
    await preload(onEvent);
    if (action === 'connect') {
      const directory = await fs.realpath(runDirectory),
        runs = await fs.realpath(path.join(root, 'data/sessions'));
      if (path.dirname(directory) !== runs)
        throw Error('Expected a fresh IZlauncher run directory');
      for (const name of ['result.json', 'connection.json'])
        if (
          await fs.access(path.join(directory, name)).then(
            () => true,
            () => false,
          )
        )
          throw Error('This console session has already connected or ended');
      const file = path.join(directory, 'connect.txt');
      if (Date.now() - (await fs.stat(file)).mtimeMs > 120000)
        throw Error('The connection command expired');
      const command = (await fs.readFile(file, 'utf8')).trim();
      const words = command.match(/^V=START\(([0-9,]+)\)$/)?.[1].split(',');
      if (
        !words ||
        words.length !== value.bootstrap.arity ||
        Number(words[0]) !== configuration.settings.consolePort ||
        words.slice(1, 5).join('.') !== configuration.settings.consoleHost
      )
        throw Error(
          'The launch command does not match the selected loader, address and port',
        );
      return input.performPlan(
        {
          id: requestId,
          steps: require('../engines/common.cjs').connectSteps({
            command,
            bootstrapName: await connectionBootstrapName(recovery),
            preloaded: value.preloaded === true,
          }),
        },
        onEvent,
      );
    }
    if (action === 'bootstrap')
      return input.performPlan(
        {
          id: requestId,
          steps: [
            {
              type: 'line',
              text: 'LIST ' + (value.bootstrap.sourceSlot ?? 0) + ':1',
              delayMs: 2000,
            },
          ],
          sourceFile: value.bootstrap.file,
        },
        onEvent,
      );
    if (action.endsWith('-editor'))
      steps.push({ type: 'key', key: 'f4', delayMs: 2000 });
    if (action.startsWith('verify-'))
      for (const command of (await sourcePlan(root, recovery)).commands)
        steps.push({ type: 'line', text: command, delayMs: 2000 });
    else if (action.startsWith('save-'))
      steps.push({
        type: 'line',
        text: slots.commands(
          value.bootstrap.saveName,
          value.bootstrap.sourceSlot,
        ).save,
        delayMs: 2000,
      });
    else throw Error('Unknown source installation action');
    return input.performPlan({ id: requestId, steps }, onEvent);
  }
  return {
    id,
    profileFile,
    profile,
    transport,
    installation,
    connectionBootstrapName,
    sourceChecks,
    recoveryAvailable: false,
    input: {
      inspect: () => input.inspect(),
      halt: (id) => input.halt(id),
      perform,
      performPlan: (...args) => input.performPlan(...args),
    },
    environment: {
      ...configuration.env,
      IZ15_LOADER: id,
      IZ15_LOADER_SETTINGS: JSON.stringify(settings),
    },
  };
}
module.exports = {
  get IDS() {
    return engines.IDS;
  },
  selected,
  create,
};
