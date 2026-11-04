'use strict';
// IZ2S Core owns its host policy; shared platform helpers describe launch plans.
const path = require('node:path');
const platform = require('../common.cjs');
const contract = require('./runtime/contract.cjs');
const manifest = require('./manifest.json'), menu = require('./menu.json');
function plan(value = {}, request = {}) {
  const selected = contract.options(value);
  const {host, node} = platform.base(request);
  const {port} = contract.endpoint(host, request.port ?? request.consolePort);
  const bootstrap = contract.bootstrap();
  contract.verify();
  if (request.runtime === 'izlauncher')
    return platform.izPlan({id:manifest.id, manifest, settings:selected, options:{...request, port}, bootstrap});
  if (request.runtime && request.runtime !== 'original') throw Error('Unsupported engine runtime');
  const held = request.rpcMode ?? selected.rpcMode;
  if (typeof held !== 'boolean') throw Error('RPC mode must be boolean');
  const cwd = path.join(__dirname, 'upstream');
  const startup = platform.startup({root:__dirname, manifest, bootstrap:bootstrap.file,
    saveName:bootstrap.saveName, arity:bootstrap.arity, runRoot:path.join(cwd,'runs'), persistent:held,
    readyPatterns:held ? ['^4/4 verified\\. SmileBASIC RPC'] : [],
    completedPatterns:held ? [] : ['^FS execution verified\\.']});
  Object.assign(startup, {preloaded:false, sourceSetup:true}); startup.bootstrap.sourceSlot = 0;
  return {loaderId:manifest.id, mode:'original', runtime:'original', runner:'upstream', command:node,
    args:['run.cjs', ...(held ? ['--rpc'] : []), host], cwd,
    env:{NS2_BASIC_ARITY:String(bootstrap.arity), NS2_RPC_MODE:held ? '1' : '0', HB_PORT:String(port)},
    startup, bootstrap:startup.bootstrap, manualCommands:startup.manualCommands,
    runRoot:startup.runRoot, lifecycle:startup.lifecycle,
    capabilities:{...manifest.capabilities, heldRpc:held, izPackages:false, streaming:false},
    notes:['IZ2S Core loads ' + bootstrap.saveName + ' and connects with five START arguments.',
      held ? 'RPC stays available until Stop.' : 'The FS verification completes and exits.',
      'Firmware dumping is an explicit command-line option.']};
}
const api = {manifest, menu, settings:contract.options, plan};
api.createEngine = context => require('../../core/session-api.cjs').engine(api, context);
module.exports = api;
