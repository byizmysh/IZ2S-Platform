const {spawn} = require('node:child_process');
const net = require('node:net');
const fs = require('node:fs');
function presets(platform = process.platform) {
  const items = [{id: 'filezilla', name: 'FileZilla', executable: platform === 'win32' ? 'filezilla.exe' : platform === 'darwin' ? '/Applications/FileZilla.app/Contents/MacOS/filezilla' : 'filezilla'}];
  if (platform === 'win32') items.unshift({id:'explorer',name:'Windows File Explorer · FTP',executable:'explorer.exe'});
  if (platform === 'linux') items.push(...['dolphin', 'nautilus', 'nemo', 'thunar'].map(id => ({id, name: id[0].toUpperCase() + id.slice(1), executable: id})));
  if (platform === 'darwin') items.unshift({id: 'finder', name: 'Finder · FTP (read-only)', executable: '/usr/bin/open'});
  items.push({id: 'system', name: 'System FTP handler', executable: ''}, {id: 'custom', name: 'Custom file manager', executable: ''});
  return items;
}
function command({manager = 'filezilla', executable = '', url}, platform = process.platform) {
  const target = new URL(url);
  if (target.protocol !== 'ftp:' || !net.isIPv4(target.hostname) || target.username || target.password || target.search || target.hash)
    throw Error('Expected an anonymous FTP URL for the selected console');
  const preset = presets(platform).find(item => item.id === manager);
  if (!preset) throw Error('Unknown file manager');
  if (manager === 'explorer') return ['explorer.exe',[target.href]];
  if (manager === 'system') {
    if (platform === 'win32') return ['rundll32.exe', ['url.dll,FileProtocolHandler', target.href]];
    return [platform === 'darwin' ? '/usr/bin/open' : 'xdg-open', [target.href]];
  }
  if (manager === 'finder') return ['/usr/bin/open', ['-a', 'Finder', target.href]];
  let program = executable.trim() || preset.executable;
  if (!program || /[\r\n\0]/.test(program)) throw Error('Enter the file manager executable path');
  if (platform === 'win32' && manager === 'filezilla' && !executable) {
    const installed = 'C:\\Program Files\\FileZilla FTP Client\\filezilla.exe';
    if (fs.existsSync(installed)) program = installed;
  }
  return [program, [target.href]];
}
async function open(settings, {platform = process.platform, launch = spawn} = {}) {
  const [program, args] = command(settings, platform);
  await new Promise((resolve, reject) => {
    const child = launch(program, args, {detached: true, stdio: 'ignore', shell: false});
    child.once('error', error => reject(Error('Could not open the file manager: ' + error.message)));
    child.once('spawn', () => {child.unref(); resolve();});
  });
  return {ok: true};
}
module.exports = {presets, command, open};
