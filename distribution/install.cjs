// Shared per-user setup. OS entry files bootstrap Node; no device commands are run here.
const fs = require('node:fs/promises'), sync = require('node:fs'), path = require('node:path'), os = require('node:os');
const {spawn} = require('node:child_process'), crypto = require('node:crypto');
const defaults = require('./runtime-downloads.json');
const SOURCE = path.resolve(__dirname, '..');
function run(program, args, options = {}) {
  return new Promise((resolve, reject) => {
    const child = spawn(program, args, {stdio: 'inherit', ...options});
    child.once('error', reject);
    child.once('close', (code, signal) => code === 0 ? resolve() : reject(Error(path.basename(program) + ' failed (' + (signal || code) + ')')));
  });
}
function defaultDestination(version, {platform = process.platform, home = os.homedir(), env = process.env} = {}) {
  const join = platform === 'win32' ? path.win32.join : path.join;
  const base = platform === 'win32' ? join(env.LOCALAPPDATA || join(home, 'AppData/Local'), 'IZ2S Platform') :
    platform === 'darwin' ? join(home, 'Library/Application Support/IZ2S Platform') : join(env.XDG_DATA_HOME || join(home, '.local/share'), 'iz2s-platform');
  return join(base, 'releases', version + '-' + new Date().toISOString().replace(/[^0-9]/g, ''));
}
async function copySource(source, destination) {
  const profile = JSON.parse(await fs.readFile(path.join(source, 'distribution/profile.json')));
  const top = new Set(['core','panel','engines','shells','hardware','profiles','sdk','tools','language','distribution','.github','docs']);
  const docImages = new Set(['connection','app-library','izlauncher','izpslauncher'].map(name => 'docs/images/' + name + '.png'));
  const rootFiles = new Set(['start.cjs','start.sh','install.sh','install-desktop.sh','Install.cmd','Start.cmd','Install.command','Start.command','Install-Linux.desktop','package.json','LICENSE','COPYRIGHT','LICENSE-SCOPE.md','THIRD_PARTY.json','README.md','FAQ.md','COMPATIBILITY.md','RELEASE-NOTES.md','SUPPORT.md','.gitignore','preview.sh']);
  const privateDirs = new Set(['.git','node_modules','__pycache__','.versions','data','runs','downloads','test-data','tests']);
  const privateFiles = new Set(['secret','capsule.key','installed.json','.engine-install.json']);
  let manifest;
  try {const document=JSON.parse(await fs.readFile(path.join(source, 'SOURCE-MANIFEST.json')));manifest=document?.files;
    if(!manifest||typeof manifest!=='object'||Array.isArray(manifest))throw Error('Invalid source manifest');}
  catch (error) {if (error.code !== 'ENOENT') throw error;}
  if(manifest&&(typeof manifest!=='object'||Array.isArray(manifest)))throw Error('Invalid source manifest');
  function allowed(relative) {
    if (relative === 'packages/iz2s-arm-input.tar.gz') return true;
    const parts = relative.split('/'), name = parts.at(-1);
    if (parts.length === 1) return rootFiles.has(name);
    if (parts.some(p => privateDirs.has(p) || /^\.(?:download|publish|placeholder)-/.test(p)) || privateFiles.has(name) || /\.(?:log|pyc|tmp|izm|pem|key)$/.test(name)) return false;
    if (parts[0] === 'desktop') return ['desktop/install.cjs','desktop/izlauncher.svg'].includes(relative);
    if (parts[0] === 'docs') return docImages.has(relative);
    if (parts[0] === 'modules') return relative === 'modules/README.md' || parts.length > 2 && profile.moduleDirectories.includes(parts[1]) && !parts.slice(2).some(p => p === 'runtime' || p === 'docs');
    if (!top.has(parts[0])) return false;
    if (parts.slice(0,3).join('/') === 'engines/kurumuz/upstream') return name === 'README.md';
    return !(parts.slice(0,2).join('/') === 'core/session' && /\.(?:png|jpg|webp)$/.test(name));
  }
  if(manifest)for(const [relative,entry]of Object.entries(manifest)){
    if(!relative||relative.includes('\\')||relative.includes(':')||relative.includes('\0')||relative.split('/').some(part=>!part||part==='.'||part==='..')||
      !entry||typeof entry!=='object'||Array.isArray(entry)||!/^[a-f0-9]{64}$/.test(entry.sha256)||('executable'in entry&&typeof entry.executable!=='boolean'))throw Error('Invalid source manifest entry');
    if(!allowed(relative))continue;
    const file=path.join(source,...relative.split('/'));
    let stat;try{stat=await fs.lstat(file);}catch(error){if(error.code==='ENOENT')throw Error('Release source is incomplete: '+relative);throw error;}
    if(!stat.isFile()||stat.isSymbolicLink())throw Error('Invalid source file: '+relative);
    const bytes=await fs.readFile(file);
    if(crypto.createHash('sha256').update(bytes).digest('hex')!==entry.sha256||('bytes'in entry&&entry.bytes!==bytes.length))throw Error('Source checksum mismatch: '+relative);
  }
  let count = 0;
  async function walk(folder, prefix = '') {
    for (const entry of await fs.readdir(folder, {withFileTypes: true})) {
      const relative = prefix + entry.name;
      if (entry.isDirectory()) {
        if (privateDirs.has(entry.name) || entry.name === 'dist' || (!prefix && !top.has(entry.name) && !['desktop','modules','packages'].includes(entry.name))) continue;
        if (relative.startsWith('modules/') && !profile.moduleDirectories.includes(entry.name) && prefix === 'modules/') continue;
        await walk(path.join(folder, entry.name), relative + '/');
      } else if (allowed(relative)) {
        if (!entry.isFile()) throw Error('Unexpected source symlink: ' + relative);
        const bytes = await fs.readFile(path.join(source, ...relative.split('/')));
        if (manifest && (!manifest[relative] || crypto.createHash('sha256').update(bytes).digest('hex') !== manifest[relative].sha256)) throw Error('Source checksum mismatch: ' + relative);
        const target = path.join(destination, ...relative.split('/'));
        await fs.mkdir(path.dirname(target), {recursive: true});
        const mode=manifest?.[relative]?.executable===true?0o755:(await fs.stat(path.join(source, ...relative.split('/')))).mode&0o777;
        await fs.writeFile(target, bytes, {mode});
        if(manifest?.[relative]?.executable===true)await fs.chmod(target,mode);
        count++;
      }
    }
  }
  await walk(source);
  return count;
}
async function extract(archive, destination, {platform = process.platform, execute = run} = {}) {
  await fs.mkdir(destination, {recursive: true});
  if (archive.endsWith('.zip')) await execute('powershell.exe', ['-NoProfile','-ExecutionPolicy','Bypass','-File',path.join(SOURCE,'distribution/extract-zip.ps1'),'-Archive',archive,'-Destination',destination]);
  else await execute('tar', ['-xzf', archive, '-C', destination]);
}
async function findProgram(folder, filename, depth = 2) {
  const candidate = path.join(folder, filename);
  if (sync.existsSync(candidate)) return candidate;
  if (depth) for (const entry of await fs.readdir(folder, {withFileTypes: true})) if (entry.isDirectory()) {
    const found = await findProgram(path.join(folder, entry.name), filename, depth - 1);
    if (found) return found;
  }
  return null;
}
async function shortcut(root, {platform = process.platform, execute = run} = {}) {
  if (platform === 'win32') return execute('powershell.exe', ['-NoProfile','-ExecutionPolicy','Bypass','-File',path.join(root,'distribution/shortcut-windows.ps1'),'-Root',root]);
  if (platform === 'darwin') {
    const app = path.join(os.homedir(),'Applications/IZ2S Platform.app/Contents');
    await fs.mkdir(path.join(app, 'MacOS'), {recursive: true});
    const quote = value => "'" + value.replace(/'/g, "'\\''") + "'";
    await fs.writeFile(path.join(app, 'MacOS/IZ2S'), '#!/bin/sh\nexec /usr/bin/open -a Terminal ' + quote(path.join(root,'Start.command')) + '\n', {mode: 0o755});
    await fs.writeFile(path.join(app, 'Info.plist'), '<?xml version="1.0" encoding="UTF-8"?><!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd"><plist version="1.0"><dict><key>CFBundleIdentifier</key><string>io.iz2s.platform</string><key>CFBundleName</key><string>IZ2S Platform</string><key>CFBundleExecutable</key><string>IZ2S</string><key>CFBundlePackageType</key><string>APPL</string></dict></plist>\n');
  }
}
async function install({source = SOURCE, destination, nodeHome, platform = process.platform, arch = process.arch,
  uvProgram, execute = run, download = require('./download-runtime.cjs').download,
  createShortcut = true, launch = true} = {}) {
  const version = JSON.parse(await fs.readFile(path.join(source,'package.json'))).version;
  if (!['linux','darwin','win32'].includes(platform) || !['x64','arm64'].includes(arch)) throw Error('A 64-bit Windows, macOS or Linux host is required');
  if (platform === 'win32' && arch !== 'x64') throw Error('Windows x64 is required. Windows ARM64 and 32-bit Windows are not supported.');
  destination ||= defaultDestination(version, {platform});
  if (!path.isAbsolute(destination)) throw Error('Installation folder must be absolute');
  const relative=path.relative(path.resolve(source),path.resolve(destination));
  if(!relative||(!relative.startsWith('..'+path.sep)&&relative!=='..'&&!path.isAbsolute(relative)))throw Error('Choose an installation folder outside the extracted source folder');
  await fs.mkdir(path.dirname(destination), {recursive: true});
  await fs.mkdir(destination); // Never merge with or replace an existing installation.
  let completed = false;
  try {
    console.log('Preparing a new installation: ' + destination);
    const files = await copySource(source, destination);
    const runtime = path.join(destination, 'data/runtime');
    await fs.mkdir(runtime, {recursive: true});
    if (!nodeHome || !sync.existsSync(path.join(nodeHome, platform === 'win32' ? 'node.exe' : 'bin/node'))) throw Error('A complete private Node.js runtime is required');
    await fs.cp(nodeHome, path.join(runtime,'node'), {recursive: true, verbatimSymlinks: true});
    const downloads = path.join(runtime, 'downloads'); await fs.mkdir(downloads);
    let uv = uvProgram;
    if (!uv) {
      const asset = defaults.uv[platform+'-'+arch]; if (!asset) throw Error('No managed Python bootstrap for this host');
      console.log('Downloading and checking Python installer (uv ' + defaults.uvVersion + ')…');
      const archive = await download('https://github.com/astral-sh/uv/releases/download/'+defaults.uvVersion+'/'+asset.file, path.join(downloads,asset.file),asset.sha256);
      await extract(archive,path.join(runtime,'uv'),{platform,execute});
      uv = await findProgram(path.join(runtime,'uv'),platform === 'win32' ? 'uv.exe' : 'uv');
      if (!uv) throw Error('Python installer executable is missing');
    }
    const env = {...process.env, UV_PYTHON_INSTALL_DIR:path.join(runtime,'python'), UV_CACHE_DIR:path.join(runtime,'cache'),
      UV_PYTHON_INSTALL_BIN:'0', UV_PYTHON_INSTALL_REGISTRY:'0', UV_NO_CONFIG:'1', PYTHONUTF8:'1', PYTHONNOUSERSITE:'1',
      IZ2S_ROOT:destination, IZ15_ROOT:destination};
    const options = {cwd:destination,env};
    console.log('Preparing private Python ' + defaults.python + ' and Pillow ' + defaults.pillow + '…');
    await execute(uv, ['python','install',defaults.python,'--no-bin'], options);
    await execute(uv, ['venv','--managed-python','--python',defaults.python,path.join(runtime,'venv')], options);
    const python = path.join(runtime,'venv',platform === 'win32' ? 'Scripts/python.exe' : 'bin/python3');
    env.IZ2S_PYTHON = python;
    await execute(uv, ['pip','install','--python',python,'--only-binary',':all:','--index-url','https://pypi.org/simple','Pillow=='+defaults.pillow], options);
    if (platform === 'win32') {
      try {await execute('ssh.exe',['-V'],{...options,stdio:'ignore'});}
      catch {
        console.log('Preparing the portable OpenSSH client for ARM input…');
        const asset = defaults.openssh[platform+'-'+arch];
        const archive = await download('https://github.com/PowerShell/Win32-OpenSSH/releases/download/'+defaults.opensshVersion+'/'+asset.file,path.join(downloads,asset.file),asset.sha256);
        await extract(archive,path.join(runtime,'openssh'),{platform,execute});
        if (!await findProgram(path.join(runtime,'openssh'),'ssh.exe')) throw Error('OpenSSH client executable is missing');
      }
    }
    require('../core/platform/runtime.cjs').configure(destination,{env,platform});
    const node = path.join(runtime,'node',platform === 'win32' ? 'node.exe' : 'bin/node');
    console.log('Checking the installed package…');
    await execute(node, ['-e','require(process.argv[1]).publish(process.argv[2]).then(r=>{if(r.errors.length)throw Error(JSON.stringify(r.errors))})',path.join(destination,'sdk/publish-modules.cjs'),destination],options);
    await execute(node, [path.join(destination,'distribution/verify.cjs')],options);
    await execute(node, [path.join(destination,'hardware/dependencies.cjs'),'--host-only','--ssh'],options);
    const state = path.join(destination,'data/state'); await fs.mkdir(state,{recursive:true});
    const profile=JSON.parse(await fs.readFile(path.join(destination,'distribution/profile.json')));
    await fs.writeFile(path.join(state,'installation.json'),JSON.stringify({status:'complete',version,platform,arch,files,installedAt:new Date().toISOString(),
      nativeHostDeclared:profile.hostPlatforms?.includes(platform+'-'+arch)===true,nativeHostQualified:false},null,2)+'\n');
    if (createShortcut) await shortcut(destination,{platform,execute});
    await fs.rm(downloads,{recursive:true,force:true});
    completed = true;
    console.log('Installed. Open ' + (platform === 'win32' ? 'Start.cmd' : platform === 'darwin' ? 'Start.command' : 'start.sh') + ' in: ' + destination);
    if (platform !== 'linux') console.log('Experimental host: installation, networking and a full console session still require testing on this OS. See COMPATIBILITY.md.');
    if (launch) {
      await require('./start-local.cjs').launch(destination,{node,env});
      console.log('Panel startup requested. If no window appears, open data/state/startup.log or run the Start file again.');
    }
    return {destination,version,platform,arch};
  } catch (error) {
    if (!completed) console.error('Setup did not complete. The new folder was kept for diagnosis; existing installations were not modified. Remove this incomplete folder before retrying with the same destination.');
    throw error;
  }
}
async function main(args = process.argv.slice(2)) {
  const options = {};
  for (let i=0;i<args.length;i++) {
    const flag=args[i];
    if (['--destination','--node-home','--uv'].includes(flag)) {if(!args[i+1])throw Error('Missing value for '+flag);options[{'--destination':'destination','--node-home':'nodeHome','--uv':'uvProgram'}[flag]]=args[++i];}
    else if(flag==='--no-shortcut')options.createShortcut=false;
    else if(flag==='--no-launch')options.launch=false;
    else throw Error('Unknown option: '+flag);
  }
  return install(options);
}
if(require.main===module)main().catch(error=>{console.error('IZ2S setup: '+error.message);process.exitCode=1;});
module.exports = {install,copySource,defaultDestination,extract,findProgram,run,shortcut};
