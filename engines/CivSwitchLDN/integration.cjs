'use strict';
const fs = require('node:fs/promises'), path = require('node:path');
const {execFile} = require('node:child_process'), {promisify} = require('node:util');
const exec = promisify(execFile);
const REQUIRED_KEYS = ['aes_kek_generation_source', 'aes_key_generation_source', 'master_key_00', 'master_key_12'];
function paths(root) {
  const runtime = path.join(root, 'data/runtime/civswitchldn');
  return {folder: path.join(root, 'engines/CivSwitchLDN'), runtime,
    python: path.join(runtime, 'bin/python'), helper: path.join(runtime, 'bin/python-ldn'),
    keys: path.join(root, 'data/private/civswitchldn/prod.keys')};
}
function validateKeys(content) {
  if (typeof content !== 'string' || Buffer.byteLength(content) > 65536 || content.includes('\0'))
    throw Error('Choose a valid prod.keys file smaller than 64 KB');
  const entries = new Map();
  for (const line of content.split(/\r?\n/)) {
    const match = /^\s*([a-z0-9_]+)\s*=\s*([0-9a-f]+)\s*(?:#.*)?$/i.exec(line);
    if (match) entries.set(match[1].toLowerCase(), match[2]);
  }
  const missing = REQUIRED_KEYS.filter(name => !/^[a-f0-9]{32}$/i.test(entries.get(name) || ''));
  if (missing.length) throw Error('prod.keys is missing valid LDN entries: ' + missing.join(', '));
}
async function importKeys(root, content) {
  validateKeys(content);
  const file = paths(root).keys;
  await fs.mkdir(path.dirname(file), {recursive: true, mode: 0o700});
  await fs.chmod(path.dirname(file), 0o700);
  const temporary = file + '.tmp-' + require('node:crypto').randomUUID();
  try {
    await fs.writeFile(temporary, content, {mode: 0o600, flag: 'wx'});
    await fs.rename(temporary, file);
  } finally { await fs.rm(temporary, {force: true}); }
  return {ok: true};
}
async function inspect(root, {radioRoot = '/sys/class/ieee80211', run = exec, platform = process.platform} = {}) {
  const p = paths(root);
  if(platform!=='linux')return {supported:false,radios:[],keysReady:false,dependenciesReady:false,capabilitiesReady:false,networkManager:false,capabilityCommand:''};
  const helperPath = await fs.realpath(p.helper).catch(() => p.helper);
  const radios = await fs.readdir(radioRoot).catch(() => []);
  let keysReady = false;
  try {validateKeys(await fs.readFile(p.keys, 'utf8')); keysReady = true;} catch {}
  let dependenciesReady = false, capabilitiesReady = false, networkManager = false;
  try {
    await run(p.python, ['-c', 'import ldn, trio; import sys; assert sys.version_info >= (3, 12)'], {timeout: 10000});
    require(require.resolve('terser', {paths: [p.folder]}));
    await fs.access(p.helper, require('node:fs').constants.X_OK);
    dependenciesReady = true;
  } catch {}
  try {
    const result = await run('getcap', [p.helper], {timeout: 3000});
    capabilitiesReady = /cap_net_admin/.test(result.stdout) && /cap_net_raw/.test(result.stdout) && /=\s*e[pi]*\b/.test(result.stdout);
  } catch {}
  try { await run('nmcli', ['-t', '-f', 'RUNNING', 'general'], {timeout: 3000}); networkManager = true; } catch {}
  return {supported: platform === 'linux', radios: radios.filter(name => /^phy\d+$/.test(name)).sort(),
    keysReady, dependenciesReady, capabilitiesReady, networkManager,
    capabilityCommand: "sudo setcap cap_net_admin,cap_net_raw=ep '" + helperPath.replace(/'/g, "'\\''") + "'"};
}
module.exports = {paths, validateKeys, importKeys, inspect};
