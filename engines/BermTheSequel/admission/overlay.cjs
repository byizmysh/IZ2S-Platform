'use strict';
// Pure staging into a NEW per-run workspace. Original upstream is never edited.
const fs = require('node:fs/promises'), C = require('node:fs').constants;
const path = require('node:path'), crypto = require('node:crypto');
const {privateConfiguration} = require('./scoped-rpc.cjs');
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const PIN = {
  'switch2/memory/fs-memory-control.cjs': 'b2236a98e06acb79daf302575dde06e2ba0df93b14ac7e5571e1f2cef842b65c',
  'switch2/pipeline.cjs': '4a984af0da9fdbc3097b714092ab7a47ad4477e6821d3c82704e657349f09379',
  'switch2/acquire.cjs': '7dfdc3d20f3660ac95e6fecbc69eeab58241f980972d242ec68a081481a9d374',
};
function once(source, old, replacement) {
  if (source.split(old).length !== 2) throw Error('FS admission source anchor differs');
  return source.replace(old, replacement);
}
// Caller owns the fresh workspace. Reject copied links/hardlinks before the
// first overlay mutation; O_NOFOLLOW also protects each final open. This is
// not a defence against a hostile process with the same UID replacing parents.
async function validateTargets(workspace, names) {
  if (!path.isAbsolute(workspace) || path.resolve(workspace) !== workspace)
    throw Error('Fresh absolute FS admission workspace required');
  const root = await fs.lstat(workspace);
  if (!root.isDirectory() || root.isSymbolicLink() || root.uid !== process.getuid())
    throw Error('FS admission workspace ownership differs');
  for (const name of names) {
    let parent = workspace;
    for (const part of name.split('/').slice(0, -1)) {
      parent = path.join(parent, part);
      const stat = await fs.lstat(parent);
      if (!stat.isDirectory() || stat.isSymbolicLink() || stat.uid !== process.getuid())
        throw Error('FS admission workspace parent must be owned and unlinked');
    }
    const stat = await fs.lstat(path.join(workspace, name)).catch(error => {
      if (error.code === 'ENOENT') return null; throw error;
    });
    if (stat && (!stat.isFile() || stat.isSymbolicLink() || stat.nlink !== 1 || stat.uid !== process.getuid()))
      throw Error('FS admission overlay target must be an owned single-link regular file');
  }
}
async function prepare({workspace, admission, expectedConnection}) {
  if (!admission || typeof admission !== 'object' || !path.isAbsolute(admission.configurationFile || '') ||
    !path.isAbsolute(admission.clientModule || '') || !/^[a-f0-9]{64}$/.test(admission.clientModuleSha256 || ''))
    throw Error('Explicit private FS admission configuration and pinned client source required');
  const config = privateConfiguration(admission.configurationFile);
  if (!expectedConnection || config.connection.port !== expectedConnection.port ||
    config.connection.token !== expectedConnection.token ||
    JSON.stringify(config.connection.addresses) !== JSON.stringify(expectedConnection.addresses))
    throw Error('Private FS admission transport differs from the original host');
  const client = await fs.readFile(admission.clientModule);
  if (client.length > 131072 || sha(client) !== admission.clientModuleSha256)
    throw Error('FS admission client source differs');
  const targets = [...Object.keys(PIN), 'shared/admission-scoped-rpc.cjs',
    'shared/admission-client.cjs', 'shared/rpc.cjs'];
  await validateTargets(workspace, targets);
  const shared = path.join(workspace, 'shared'), hashes = {};
  const sources = {};
  for (const [name, pin] of Object.entries(PIN)) {
    const bytes = await fs.readFile(path.join(workspace, name));
    if (sha(bytes) !== pin) throw Error('Unsupported FS admission upstream source');
    sources[name] = bytes.toString('utf8');
  }
  let control = sources['switch2/memory/fs-memory-control.cjs'];
  control = once(control, "const hash = bytes => crypto.createHash('sha256').update(bytes).digest('hex');",
    "const hash = bytes => crypto.createHash('sha256').update(bytes).digest('hex');\nconst admission = require('../../shared/rpc.cjs').admission;\nif (!admission) throw Error('Required per-run FS admission unavailable');");
  const originalDispatch = `        const request = JSON.parse(text.slice(0, text.indexOf('\\n')));
        const controlled = runControl && !['info', 'retire'].includes(request.command);
        if (controlled) await runControl.begin('memory ' + request.command);
        const result = await dispatch(request);
        if (controlled) await runControl.end('memory ' + request.command);
        busy = false;
        socket.end(JSON.stringify(result) + '\\n');
        if (request.command === 'retire') {server.close(); finish();}`;
  const wrappedDispatch = `        let command;
        const serialized = await admission.outer(text.slice(0, text.indexOf('\\n')), socket, async request => {
          command = request.command;
          const controlled = runControl && !['info', 'retire'].includes(request.command);
          if (controlled) await runControl.begin('memory ' + request.command);
          const result = await dispatch(request);
          if (controlled) await runControl.end('memory ' + request.command);
          return result;
        });
        busy = false;
        socket.end(serialized);
        if (command === 'retire') {server.close(); finish();}`;
  control = once(control, originalDispatch, wrappedDispatch);
  control = once(control, '      } catch (error) {\n        await writeAt(address, before);',
    '      } catch (error) {\n        if (admission.retained()) throw error;\n        await writeAt(address, before);');
  const catchStart = "        if (error instanceof StopRequested) {";
  const catchEnd = "      } finally {\n        busy = false;";
  if (control.split(catchStart).length !== 2 || control.split(catchEnd).length !== 2) throw Error('FS admission catch anchor differs');
  const begin = control.indexOf(catchStart), end = control.indexOf(catchEnd, begin);
  if (end <= begin) throw Error('FS admission catch bounds differ');
  control = control.slice(0, begin) + `        const retained = await admission.outerFailure(error);
        if (retained) {
          state.nativeAdmissionRetained = true;
          state.error = 'FS admission dispatch retained; no retry';
          await save();
        }
        if (!socket.destroyed) socket.end(JSON.stringify({error: retained ?
          'FS admission dispatch retained; no retry' : 'FS admission dispatch fenced',
          retained, admissionFenced: !retained}) + '\\n');
` + control.slice(end);
  control = once(control, "        if (retiring) {server.close(); finish();}",
    "        if (retiring && !admission.retained()) {server.close(); finish();}");
  control = once(control, "  server.on('error', fail);", "  server.on('error', () => { void admission.retainAuthority(); });");
  control = once(control, `  const stop = () => {
    retiring = true;
    if (!busy) {server.close(); finish();}
  };`, `  const stop = () => { void admission.retainAuthority(); };`);
  control = once(control, '    state.ready = true;\n    await save();',
    '    state.nativeAdmission = await admission.endpointReady({session: live.session, socketPath});\n    state.ready = true;\n    await save();');
  sources['switch2/memory/fs-memory-control.cjs'] = control;
  // An admitted failure must never enter the upstream retry/abandon/kill path.
  sources['switch2/acquire.cjs'] = once(sources['switch2/acquire.cjs'],
    "      if (!error || typeof error !== 'object') error = new Error(String(error));",
    "      await require('../shared/rpc.cjs').admission.retainAuthority();\n      await require('../shared/rpc.cjs').admission.park();\n      if (!error || typeof error !== 'object') error = new Error(String(error));");
  sources['switch2/pipeline.cjs'] = once(sources['switch2/pipeline.cjs'],
    '    record.error = error.stack;',
    "    await require('../shared/rpc.cjs').admission.retainAuthority();\n    await require('../shared/rpc.cjs').admission.park();\n    record.error = 'FS admission pipeline retained';");
  const factory = await fs.readFile(path.join(__dirname, 'scoped-rpc.cjs'));
  const entry = Buffer.from("module.exports = require('./admission-scoped-rpc.cjs').open({configurationFile:" + JSON.stringify(admission.configurationFile) + '});\n');
  const writes = {...sources, 'shared/admission-scoped-rpc.cjs': factory,
    'shared/admission-client.cjs': client, 'shared/rpc.cjs': entry};
  for (const [name, content] of Object.entries(writes)) {
    const bytes = Buffer.isBuffer(content) ? content : Buffer.from(content), file = path.join(workspace, name);
    const handle = await fs.open(file, C.O_WRONLY | C.O_CREAT | C.O_NOFOLLOW, 0o600);
    try {
      const stat = await handle.stat();
      if (!stat.isFile() || stat.nlink !== 1 || stat.uid !== process.getuid())
        throw Error('FS admission overlay target ownership changed');
      await handle.chmod(0o600); await handle.truncate(0); await handle.writeFile(bytes);
    } finally { await handle.close(); }
    hashes[name] = sha(bytes);
  }
  return Object.freeze({authorityId: config.channel.authorityId, session: config.connection.addresses.session,
    timeoutMs: config.timeoutMs ?? 125000,
    outerDispatchFenced: false, nestedSdkScoped: false, runtimeConfirmed: false, sourceHashes: Object.freeze(hashes),
    rpcFile: path.join(shared, 'rpc.cjs')});
}
module.exports = {prepare, PIN};
