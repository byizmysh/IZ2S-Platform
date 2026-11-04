'use strict';
const contract = require('./runtime/contract.cjs');
const {extendNative} = require('../extend-native.cjs');
function create(request = {}) {
  const {host, port, command} = contract.endpoint(request.host, request.port);
  const selected = contract.options(request.settings);
  if (request.recovery) throw Error('IZ2S Core does not support NS2R recovery');
  contract.verify();
  const wire = require('./upstream/engine/lib/connection.cjs');
  const native = require('./upstream/engine/lib/native.cjs');
  const result = {
    id:'izmysh', command, bootstrap:contract.bootstrap(), protocol:1, resumable:false, preloaded:false,
    async listen(options = {}) {
      const link = await wire.listen({...options, host, port});
      return {connection:link.connection, addresses:{...link.addresses, protocol:1}};
    },
    native(link) {
      if (!link?.connection || !link.addresses) throw Error('Expected a connected engine console');
      return extendNative(new native.NativeClient(link.connection, link.addresses));
    },
    qualify:require('./upstream/engine/lib/qualify.cjs').qualify,
  };
  if (selected.fsRpcMode) result.startRuntime = context => require('./fs-rpc.cjs').start(context);
  return result;
}
module.exports = {create};
