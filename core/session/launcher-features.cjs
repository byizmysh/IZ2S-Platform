'use strict';
const path = require('node:path');
const {readJson} = require('./ledger.cjs');

// FS-backed workers must never be installed in a deliberately FS-free session.
// An enabled but unqualified endpoint is an error, not permission to downgrade.
async function read(directory, env = process.env) {
  const loader = await readJson(path.join(directory, 'loader.json'));
  if (!loader?.settings || typeof loader.settings !== 'object')
    throw Error('Missing launcher acquisition settings');
  const fsRpc = loader.settings.fsRpcMode === true;
  const sd = env.IZ2S_SD_LAUNCHER === '1';
  if (sd && !fsRpc) throw Error('The SD launcher requires FS RPC');
  if (fsRpc) {
    const endpoint = await readJson(path.join(directory, 'fs-rpc-state.json'));
    if (endpoint?.state !== 'ready' || endpoint.verified !== true ||
        endpoint.session !== path.basename(directory))
      throw Error('The launcher FS endpoint is not qualified for this session');
  }
  return {
    fsRpc,
    recovery: fsRpc,
    mixedMenu: fsRpc && !sd && env.IZ18_RESIDENT_GRAPHICS !== '1' &&
      env.IZ2S_MIXED_MENU !== '0',
  };
}

module.exports = {read};
