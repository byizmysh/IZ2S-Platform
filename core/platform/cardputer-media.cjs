// Optional Linux SD discovery. File/ZIP download remains available on every host.
const fs = require('node:fs/promises');
const unescapeMount = text => text.replace(/\\([0-7]{3})/g, (_, octal) => String.fromCharCode(parseInt(octal, 8)));
function mountInfo(text) {
  return text.split('\n').flatMap(line => {
    const [left, right] = line.split(' - ');
    if (!right) return [];
    const fields = left.split(' '), filesystem = right.split(' ');
    if (fields.length < 6 || filesystem.length < 2) return [];
    return [{source: unescapeMount(filesystem[1]), target: unescapeMount(fields[4]), fstype: filesystem[0]}];
  });
}
async function discover({platform = process.platform, readDir = fs.readdir, realpath = fs.realpath,
  readFile = fs.readFile, findMounts} = {}) {
  if (platform !== 'linux') return [];
  const aliases = await readDir('/dev/disk/by-id').catch(error => {if (error.code === 'ENOENT') return []; throw error;});
  const devices = new Set();
  for (const alias of aliases) if (/^usb-M5Stack.*Launcher.*SD.*-part\d+$/i.test(alias))
    devices.add(await realpath('/dev/disk/by-id/' + alias));
  if (!devices.size) return [];
  const mounted = findMounts ? await findMounts() :
    {filesystems: mountInfo(await readFile('/proc/self/mountinfo', 'utf8'))};
  const volumes = [];
  function visit(rows) {
    for (const row of rows || []) {
      if (devices.has(row.source) && ['vfat', 'exfat'].includes(row.fstype) && /^\/(?:run\/media|media)\//.test(row.target))
        volumes.push({target: row.target, source: row.source});
      visit(row.children);
    }
  }
  visit(mounted.filesystems);
  return volumes;
}
module.exports = {discover, mountInfo};
