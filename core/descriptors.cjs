const fs = require('node:fs'),
  path = require('node:path'),
  crypto = require('node:crypto');
const ID = /^[a-z0-9][a-z0-9-]{0,47}$/,
  VERSION = /^\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?$/;
function assert(ok, message) {
  if (!ok) throw Error(message);
}
function relative(value) {
  assert(
    typeof value === 'string' &&
      value.length > 0 &&
      !value.includes('\\') &&
      !/[\x00-\x1f\x7f:]/.test(value) &&
      !value.includes('\0') &&
      !path.isAbsolute(value) &&
      value.split('/').every((p) => p && p !== '.' && p !== '..'),
    'Path must stay inside extension',
  );
  return value;
}
function local(root, value) {
  return path.join(root, relative(value));
}
function validate(value, kind, id) {
  assert(
    value && typeof value === 'object' && !Array.isArray(value),
    'Expected descriptor',
  );
  assert(
    value.format === 'izlauncher.' + kind && value.apiVersion === 1,
    'Unsupported ' + kind + ' API',
  );
  assert(
    ID.test(value.id) && (!id || value.id === id),
    'Descriptor id must match folder',
  );
  assert(
    typeof value.name === 'string' && value.name.trim().length > 0,
    'Missing name',
  );
  assert(VERSION.test(value.version), 'Invalid version');
  if (value.entry) relative(value.entry);
  assert(
    Array.isArray(value.platforms) &&
      value.platforms.every((p) =>
        /^(linux|darwin|win32)-(x64|arm64)$/.test(p),
      ),
    'Invalid platforms',
  );
  if (kind === 'engine') {
    if (value.directory !== undefined)
      assert(typeof value.directory === 'string' && /^[A-Za-z0-9][A-Za-z0-9-]{0,47}$/.test(value.directory),
        'Invalid engine directory');
    if (value.runtimeEngine !== undefined)
      assert(typeof value.runtimeEngine === 'string' && ID.test(value.runtimeEngine) &&
        value.runtimeEngine !== value.id, 'Invalid shared runtime engine');
    assert(
      value.entry &&
        Array.isArray(value.modes) &&
        value.modes.length>0 &&
        value.modes.every(mode=>['original','izlauncher'].includes(mode)) &&
        new Set(value.modes).size===value.modes.length,
      'Engine entry and a unique supported mode required',
    );
    if (value.installation) {
      assert(
        value.installation.directory === 'upstream' &&
          value.installation.kind === 'git',
        'Invalid engine installation',
      );
      relative(value.installation.bootstrapPath);
      assert(
        Array.isArray(value.installation.requiredFiles) &&
          value.installation.requiredFiles.length > 0,
        'Missing engine file list',
      );
      value.installation.requiredFiles.forEach(relative);
    }
    if (value.bootstrap) {
      relative(value.bootstrap.file);
      assert(
        /^[a-f0-9]{64}$/.test(value.bootstrap.sha256),
        'Invalid bootstrap hash',
      );
      assert(
        Number.isInteger(value.bootstrap.installSlot) &&
          value.bootstrap.installSlot >= 0 &&
          value.bootstrap.installSlot <= 3,
        'Invalid bootstrap slot',
      );
      assert(
        /^[A-Z0-9_]{1,16}$/.test(value.bootstrap.saveName),
        'Invalid save name',
      );
      assert(
        [5, 9].includes(value.bootstrap.arity),
        'Unsupported bootstrap arity',
      );
    }
  }
  if (kind === 'shell') {
    assert(
      value.entry && Number.isInteger(value.consoleAbi),
      'Shell entry and console ABI required',
    );
    if(value.moduleShells!==undefined)assert(Array.isArray(value.moduleShells)&&value.moduleShells.length<=16&&value.moduleShells.every(item=>typeof item==='string'&&ID.test(item))&&new Set(value.moduleShells).size===value.moduleShells.length,'Invalid module shell compatibility list');
  }
  if (kind === 'profile')
    assert(
      value.entry && Array.isArray(value.fields),
      'Profile entry and fields required',
    );
  return value;
}
function moduleDescriptor(v) {
  assert(
    v?.format === 'izlauncher.module' &&
      v.formatVersion === 1 &&
      v.apiVersion === 1,
    'Unsupported module format/API',
  );
  assert(ID.test(v.id) && VERSION.test(v.version), 'Invalid module identity');
  require('./module-paths.cjs').directory(v);
  assert(
    typeof v.name === 'string' && v.name.length > 0 && v.name.length <= 64,
    'Invalid module name',
  );
  assert(
    typeof v.author === 'string' && v.author.length > 0,
    'Missing module author',
  );
  assert(v.switch || v.server, 'Module has no component');
  assert(
    Array.isArray(v.platforms) &&
      v.platforms.every((p) => /^(linux|darwin|win32)-(x64|arm64)$/.test(p)),
    'Invalid module platforms',
  );
  assert(v.requires?.platformApi === 1, 'Unsupported platform API');
  if(v.dependencies!==undefined){assert(Array.isArray(v.dependencies),'Invalid dependency list');for(const d of v.dependencies){assert(ID.test(d.id),'Invalid dependency id');if(d.kind==='module'&&d.version!==undefined)assert(typeof d.version==='string'&&/^(?:>=)?\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?$/.test(d.version),'Unsupported module version requirement');}}
  if (v.switch) {
    assert(v.switch.abi === 1, 'Unsupported console ABI');
    relative(v.switch.entry);
    assert(
      Array.isArray(v.switch.files) && v.switch.files.length > 0,
      'Missing Switch file list',
    );
    v.switch.files.forEach(relative);
    assert(
      v.switch.files.includes(v.switch.entry),
      'Switch entry absent from file list',
    );
  }
  if (v.server) {
    if(v.server.activation?.exclusiveGroup!==undefined)assert(ID.test(v.server.activation.exclusiveGroup),'Invalid module exclusive group');
    assert(v.server.runtime === 'node', 'Unsupported server runtime');
    relative(v.server.entry);
    if(v.server.settings!==undefined){assert(v.server.settings&&typeof v.server.settings==='object','Invalid module settings descriptor');relative(v.server.settings.entry);assert(v.server.settings.entry.startsWith('server/'),'Settings entry must be inside server/');if(v.server.settings.label!==undefined)assert(typeof v.server.settings.label==='string'&&v.server.settings.label.length<=48,'Invalid settings label');}
    if(v.server.localPackages!==undefined){
      assert(Array.isArray(v.server.localPackages),'Invalid local package list');
      const ids=new Set();for(const p of v.server.localPackages){
        assert(ID.test(p.id)&&!ids.has(p.id),'Invalid or duplicate local package');ids.add(p.id);
        for(const key of ['path','entry','installer'])relative(p[key]);
        assert(p.importSource===undefined||typeof p.importSource==='boolean','Invalid local import mode');
        if(p.requiredFiles!==undefined){assert(p.requiredFiles&&typeof p.requiredFiles==='object'&&!Array.isArray(p.requiredFiles),'Invalid local runtime files');
          for(const [platform,files]of Object.entries(p.requiredFiles)){assert(/^(linux|darwin|win32)-(x64|arm64)$/.test(platform)&&Array.isArray(files)&&files.length>0,'Invalid local runtime platform');for(const file of files){relative(file);assert(file.startsWith('runtime/'),'Host runtime must stay in its module');}}
        }
      }
    }
    if (v.server.assets !== undefined) {
      assert(Array.isArray(v.server.assets), 'Invalid server asset list');
      v.server.assets.forEach(relative);
    }
    if (v.server.sessionHook) {
      assert(
        typeof v.server.sessionHook === 'string' &&
          v.server.implementations?.[v.server.sessionHook],
        'Missing session hook implementation',
      );
      const a = v.server.activation;
      assert(
        a &&
          Number.isInteger(a.offset) &&
          Number.isInteger(a.sequenceOffset) &&
          a.offset >= 0 &&
          a.offset <= 1020 &&
          a.sequenceOffset >= 0 &&
          a.sequenceOffset <= 1020 &&
          Array.isArray(a.values) &&
          a.values.length > 0 &&
          a.values.every(Number.isFinite),
        'Invalid session hook activation',
      );
    }
  }
  return v;
}
function scan(root, kind) {
  const dir = path.join(
      root,
      { engine: 'engines', shell: 'shells', profile: fs.existsSync(path.join(root,'hardware/configurations')) ? 'hardware/configurations' : 'profiles' }[kind],
    ),
    items = [],
    errors = [];
  if (!fs.existsSync(dir)) return { items, errors };
  for (const entry of fs
    .readdirSync(dir, { withFileTypes: true })
    .sort((a, b) => a.name.localeCompare(b.name))) {
    if (!entry.isDirectory() || !(kind === 'engine' ? /^[A-Za-z0-9][A-Za-z0-9-]{0,47}$/.test(entry.name) : ID.test(entry.name))) continue;
    try {
      const file = path.join(dir, entry.name, kind + '.json');
      if (!fs.existsSync(file)) continue;
      const bytes = fs.readFileSync(file);
      assert(bytes.length <= 65536, 'Descriptor too large');
      const value = JSON.parse(bytes);
      const descriptor = validate(value, kind, kind === 'engine' && value.directory === entry.name ? value.id : entry.name),
        folder = path.dirname(file);
      if (kind === 'engine') assert((descriptor.directory || descriptor.id) === entry.name, 'Engine directory differs from descriptor');
      for (const key of ['entry'])
        if (descriptor[key])
          assert(
            fs.statSync(local(folder, descriptor[key])).isFile(),
            'Missing ' + key,
          );
      const installation =
        kind === 'engine'
          ? require('./engine-install.cjs').status(folder, descriptor)
          : null;
      if (descriptor.bootstrap && (!installation || installation.installed))
        assert(
          crypto
            .createHash('sha256')
            .update(fs.readFileSync(local(folder, descriptor.bootstrap.file)))
            .digest('hex') === descriptor.bootstrap.sha256,
          'Bootstrap checksum mismatch',
        );
      items.push({
        ...descriptor,
        ...(installation
          ? {
              installed: installation.installed,
              installationStatus: installation,
            }
          : {}),
        folder,
        digest: crypto.createHash('sha256').update(bytes).digest('hex'),
        supported: descriptor.platforms.includes(
          process.platform + '-' + process.arch,
        ),
      });
    } catch (e) {
      errors.push({ id: entry.name, kind, error: e.message });
    }
  }
  items.sort(
    (a, b) =>
      (a.order ?? 100) - (b.order ?? 100) || a.name.localeCompare(b.name),
  );
  return { items, errors };
}
module.exports = {
  ID,
  VERSION,
  assert,
  relative,
  local,
  validate,
  moduleDescriptor,
  scan,
};
