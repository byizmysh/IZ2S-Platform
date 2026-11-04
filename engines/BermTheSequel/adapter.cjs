const path = require('node:path');
const {startup, base, izPlan} = require('../common.cjs');
const descriptor = require('./engine.json');
const manifest = require('./manifest.json'), menu = require('./menu.json');

function settings(value = {}) {
  if (value.rpcMode !== undefined && typeof value.rpcMode !== 'boolean')
    throw Error('RPC mode must be a checkbox value');
  if (value.fsRpcMode !== undefined && typeof value.fsRpcMode !== 'boolean')
    throw Error('FS RPC mode must be a checkbox value');
  return {rpcMode: value.rpcMode === true, fsRpcMode: value.fsRpcMode === true};
}

function plan(value = {}, options = {}) {
  const selected = settings(value), {host, node} = base(options);
  const cwd = path.join(__dirname, 'upstream');
  require('./upstream/engine/check-runtime.cjs').verify();
  const bootstrap = {
    file: path.join(__dirname, descriptor.bootstrap.file),
    saveName: descriptor.bootstrap.saveName,
    sourceSlot: 0, arity: descriptor.bootstrap.arity,
    loaderId: descriptor.id, recovery: false,
  };
  const port = Number(options.port ?? options.consolePort ?? descriptor.consolePort);
  if (!Number.isInteger(port) || port < 1024 || port > 65535)
    throw Error('Port must be 1024..65535');
  if (options.runtime === 'izlauncher')
    return izPlan({id: manifest.id, manifest, settings: selected,
      options: {...options, port}, bootstrap});
  if (options.runtime && options.runtime !== 'original')
    throw Error('Unsupported engine runtime');
  const rpcMode = options.rpcMode ?? selected.rpcMode;
  if (typeof rpcMode !== 'boolean') throw Error('RPC mode must be boolean');
  const data = startup({
    root: __dirname, manifest, bootstrap: bootstrap.file,
    saveName: bootstrap.saveName, arity: bootstrap.arity,
    runRoot: path.join(cwd, 'runs'), persistent: rpcMode,
    readyPatterns: rpcMode ? ['^4/4 verified\\. SmileBASIC RPC'] : [],
    completedPatterns: rpcMode ? [] : ['^FS execution verified\\.'],
  });
  data.preloaded = false; data.sourceSetup = true; data.bootstrap.sourceSlot = 0;
  return {
    loaderId: manifest.id, mode: 'original', runtime: 'original', runner: 'upstream',
    command: node, args: ['run.cjs', ...(rpcMode ? ['--rpc'] : []), host], cwd,
    env: {NS2_BASIC_ARITY: String(bootstrap.arity), NS2_RPC_MODE: rpcMode ? '1' : '0', HB_PORT: String(port)},
    startup: data, bootstrap: data.bootstrap, manualCommands: data.manualCommands,
    runRoot: data.runRoot, lifecycle: data.lifecycle,
    capabilities: {...manifest.capabilities, heldRpc: rpcMode, izPackages: false, streaming: false},
    notes: [
      'Launch loads saved ' + bootstrap.saveName + ' and uses exactly five START arguments.',
      rpcMode ? 'Original RPC remains live until Stop.' : 'Original FS demonstration completes and exits.',
      'No firmware dump is started by Launch.',
    ],
  };
}

module.exports = {manifest, menu, settings, plan};
module.exports.createEngine = context => require('../../core/session-api.cjs').engine(module.exports, context);
