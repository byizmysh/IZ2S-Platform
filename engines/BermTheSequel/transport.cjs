// The original acquisition bootstrap feeds the shared IZlauncher runtime.
// FS acquisition is an explicit pre-graphics hook; it shares this native bridge.
const path = require('node:path');
const net = require('node:net');
const {extendNative} = require('../extend-native.cjs');
const descriptor = require('./engine.json');

function create({host, port = descriptor.consolePort, recovery = false, settings = {}} = {}) {
  if (!net.isIPv4(host || '') || !Number.isInteger(port) || port < 1024 || port > 65535)
    throw Error('Expected reachable IPv4 address and port');
  if (!settings || typeof settings !== 'object' || Array.isArray(settings))
    throw Error('Expected engine loader settings');
  require('./upstream/engine/check-runtime.cjs').verify();
  const connection = require('./upstream/engine/lib/connection.cjs');
  const {NativeClient} = require('./upstream/engine/lib/native.cjs');
  const {qualify} = require('./upstream/engine/lib/qualify.cjs');
  const {arity, saveName} = descriptor.bootstrap;
  if(recovery)throw Error('The engine bootstrap does not support NS2R recovery');

  const bootstrap = {
    file: path.join(__dirname, descriptor.bootstrap.file),
    saveName,
    sourceSlot: 0,
    arity,
    loaderId: descriptor.id,
    recovery: false,
  };
  return {
    id: descriptor.id,
    // Use the shared authenticated RPC client selected by engines/host.cjs.
    command: `V=START(${[port, ...host.split('.')].join(',')})`,
    bootstrap,
    protocol: 1,
    resumable: false,
    // Always load the saved engine program into slot 0.
    preloaded: false,
    async listen(options = {}) {
      const connected = await connection.listen({...options, host, port});
      return {...connected, addresses: {...connected.addresses, protocol: 1}};
    },
    native(connected) {
      if (!connected?.connection || !connected.addresses)
        throw Error('Expected a connected engine console');
      return extendNative(new NativeClient(connected.connection, connected.addresses));
    },
    qualify,
    ...(settings.fsRpcMode === true ? {
      startRuntime: context => require('./fs-rpc.cjs').start(context),
    } : {}),
  };
}

module.exports = {create};
