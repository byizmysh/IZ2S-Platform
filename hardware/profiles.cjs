// IZ2s launch topologies. Resolving a profile never contacts a device.
const fs = require('node:fs/promises'),
  path = require('node:path'),
  net = require('node:net'),
  os = require('node:os'),
  crypto = require('node:crypto');
const ROOT = path.resolve(__dirname, '..');
const definitions = (root=ROOT) => {
 const actual=require('node:fs').existsSync(path.join(root,'hardware/configurations'))||require('node:fs').existsSync(path.join(root,'profiles'))?root:ROOT;
 const hardwareRoot=require('node:fs').existsSync(path.join(actual,'hardware/devices'))?actual:ROOT;
 return require('./index.cjs').configurations(actual,hardwareRoot).items;
};
const clone = (value) => JSON.parse(JSON.stringify(value));
function list(root=ROOT) {
  return definitions(root).filter(d=>d.panelVisible!==false).sort((a,b)=>(a.order??100)-(b.order??100)||a.name.localeCompare(b.name)).map((d) => ({ ...clone(d), settingsSchema: clone(d.fields) }));
}
function resolve(id = 'pc-arm', { root = ROOT, settings = {}, profile = {} } = {}) {
  const definition = definitions(root).find((d) => d.id === id);
  if (!definition) throw Error('Unknown launch configuration');
  const defaults = Object.fromEntries(definition.fields.map((f) => [f.key, f.default]));
  const value = {
    consolePort: 39415,
    panelPort: 39517,
    hidDevice: '/dev/hidg0',
    deviceFolder: '/opt/iz2s/arm-input',
    ...defaults,
    ...settings,
    ...profile,
  };
  if (definition.hardware?.combined && profile.consoleHost === undefined)
    value.consoleHost = defaults.consoleHost;
  for (const field of ['consoleHost', 'consoleAddress', 'consoleWifiAddress', 'armAddress'])
    if (value[field] && !net.isIPv4(value[field]))
      throw Error('Enter an IPv4 address for ' + field);
  for (const field of ['consolePort', 'panelPort'])
    if (!Number.isInteger(value[field]) || value[field] < 1024 || value[field] > 65535)
      throw Error('Invalid ' + field);
  if (!path.isAbsolute(value.deviceFolder) || !/^\/dev\/hidg\d+$/.test(value.hidDevice))
    throw Error('Invalid input module directory or HID gadget device');
  if(value.armUser&&!/^[a-z_][a-z0-9_-]{0,31}$/.test(value.armUser))throw Error('Invalid SSH user name');
  if(value.sshIdentity&&(!path.isAbsolute(value.sshIdentity)||/[\r\n\0]/.test(value.sshIdentity)))throw Error('SSH key path must be absolute');
  if(value.sshKnownHosts&&(!path.isAbsolute(value.sshKnownHosts)||/[\r\n\0]/.test(value.sshKnownHosts)))throw Error('SSH known-hosts path must be absolute');
  if(value.sshHostKeyAlias&&!/^[a-zA-Z0-9._-]{1,128}$/.test(value.sshHostKeyAlias))throw Error('Invalid SSH host-key alias');
  const requirements = [];
  if (definition.hardware?.combined && !definition.hardware.server.hostPlatforms.includes(process.platform+'-'+process.arch))
    requirements.push(
      'Run this configuration on '+definition.hardware.server.name+' ('+definition.hardware.server.hostPlatforms.join(', ')+').',
    );
  for(const node of definition.nodes||[])if(node.definition.validator){const validator=require(require('../core/descriptors.cjs').local(node.definition.folder,node.definition.validator));requirements.push(...validator.validate(value));}
  const env = {
    ...(value.armUser ? {IZ15_ARM_USER:value.armUser} : {}),
    ...(value.sshIdentity ? {IZ15_SSH_IDENTITY:value.sshIdentity} : {}),
    ...(value.sshKnownHosts ? {IZ15_SSH_KNOWN_HOSTS:value.sshKnownHosts} : {}),
    ...(value.sshHostKeyAlias ? {IZ15_SSH_HOST_KEY_ALIAS:value.sshHostKeyAlias} : {}),
    IZ15_LAUNCH_CONFIGURATION: id,
    IZ15_EXECUTION_HOST: definition.executionHost,
    IZ15_INPUT_ADAPTER: definition.inputAdapter,
    HB_STREAM_HOST: value.consoleHost,
    HB_PORT: String(value.consolePort),
    IZ15_ARM_ADDRESS: value.armAddress || value.consoleHost,
    IZ12_ARM_ADDRESS: value.armAddress || value.consoleHost,
    IZ15_DEVICE_FOLDER: value.deviceFolder,
    IZ15_HID_DEVICE: value.hidDevice,
  };
  for(const [key,value]of Object.entries({...env}))if(key.startsWith('IZ15_'))env[key.replace('IZ15_','IZ2S_')]=value;
  return {
    ...clone(definition),
    root: path.resolve(root),
    settings: value,
    env,
    requirements,
    setupRequired: requirements.length > 0,
  };
}
async function load(root = ROOT) {
  try {
    const saved = JSON.parse(
      await fs.readFile(path.join(root, 'data/state/launch-configurations.json'), 'utf8'),
    );
    if (!definitions(root).some((d) => d.id === saved.selected))
      throw Error('Unknown saved launch configuration');
    const definition=definitions(root).find(d=>d.id===saved.selected);
    const selected=definition.selectable===false||definition.panelVisible===false?'pc-arm':saved.selected;
    return { selected, profiles: saved.profiles || {} };
  } catch (e) {
    if (e.code !== 'ENOENT') throw e;
    return { selected: 'pc-switch1', profiles: {} };
  }
}
async function save(root = ROOT, { selected = 'pc-switch1', profiles = {} } = {}) {
  if (!profiles || typeof profiles !== 'object' || Array.isArray(profiles))
    throw Error('Invalid launch configuration settings');
  if(resolve(selected, { root, profile: profiles[selected] || {} }).selectable===false)
    throw Error('This hardware configuration is not available yet');
  for (const [id, value] of Object.entries(profiles)) resolve(id, { root, profile: value });
  const folder = path.join(root, 'data/state'),
    file = path.join(folder, 'launch-configurations.json'),
    temporary = file + '.' + crypto.randomUUID() + '.tmp',
    value = { version: 1, selected, profiles: clone(profiles) };
  await fs.mkdir(folder, { recursive: true, mode: 0o700 });
  try {
    await fs.writeFile(temporary, JSON.stringify(value, null, 2) + '\n', {
      flag: 'wx',
      mode: 0o600,
    });
    await fs.rename(temporary, file);
  } finally {
    await fs.rm(temporary, { force: true });
  }
  return value;
}
function createInputLink({ root = ROOT, configuration, ...options }) {
  configuration ??= resolve('pc-arm', { root });
  return require(definitions(root).find(d=>d.id===configuration.id).folder+'/'+configuration.entry).createInputLink({root,configuration,...options});
}
async function createChecks(options) {
 return require(definitions(options.root).find(d=>d.id===options.configuration.id).folder+'/'+options.configuration.entry).createChecks(options);
}

module.exports = { ROOT, list, resolve, save, load, createInputLink, createChecks };
