// Host networking through Node APIs. Route selection sends no probe payload.
const os = require('node:os');
const net = require('node:net');
const dgram = require('node:dgram');
const fs = require('node:fs/promises');
const {execFile} = require('node:child_process');
const {promisify} = require('node:util');
const run = promisify(execFile);

let windowsInventory;
function windowsAdapterMode(type, name, description) {
  if ([23, 24, 131].includes(type) || /(?:wireguard|wintun|tailscale|\bvpn\b|virtualbox host-only|vEthernet \((?:WSL|Default Switch)\))/i.test(`${name} ${description}`)) return 'excluded';
  if (type === 71) return 'wifi';
  if ([6, 26, 62, 69, 117].includes(type)) return 'lan';
  return 'unknown';
}

function windowsAdapterModes({execute = run, now = Date.now} = {}) {
  if (windowsInventory && now() < windowsInventory.expires) return windowsInventory.promise;
  // Match the OS interface type by IPv4, even if its alias was renamed or localized.
  const script = `[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
$rows = @([System.Net.NetworkInformation.NetworkInterface]::GetAllNetworkInterfaces() | ForEach-Object {
  $adapter = $_
  foreach ($ip in $adapter.GetIPProperties().UnicastAddresses) {
    if ($ip.Address.AddressFamily -eq 'InterNetwork') {
      @{address=$ip.Address.ToString(); type=[int]$adapter.NetworkInterfaceType; name=$adapter.Name; description=$adapter.Description}
    }
  }
})
ConvertTo-Json -InputObject $rows -Compress`;
  const promise = execute('powershell.exe', ['-NoLogo', '-NoProfile', '-NonInteractive', '-Command', script], {
    timeout: 2500, windowsHide: true, maxBuffer: 256 * 1024,
  }).then(({stdout}) => {
    const rows = JSON.parse(stdout.trim().replace(/^\uFEFF/, ''));
    const result = new Map();
    for (const row of Array.isArray(rows) ? rows : []) {
      if (!net.isIPv4(row.address)) continue;
      const mode = windowsAdapterMode(row.type, row.name, row.description);
      result.set(row.address, result.has(row.address) && result.get(row.address) !== mode ? 'unknown' : mode);
    }
    return result;
  }).catch(() => new Map());
  windowsInventory = {expires: now() + 10000, promise};
  return promise;
}

async function isTunnel(name, {platform = process.platform, access = fs.access} = {}) {
  if (/^(tailscale|tun|tap|utun|wg|amn|ppp|vpn|proton|nordlynx)/i.test(name)) return true;
  if (platform !== 'linux') return false;
  return access('/sys/class/net/' + name + '/tun_flags').then(() => true, () => false);
}

async function isWireless(name, {platform = process.platform, stat = fs.stat} = {}) {
  if (/^(?:wl|wlan)|wi[ -]?fi|wireless/i.test(name)) return true;
  if (platform !== 'linux') return false;
  // Optional interface metadata; absence does not disable networking.
  const base = '/sys/class/net/' + name;
  return stat(base + '/wireless').then(() => true, () => stat(base + '/phy80211').then(() => true, () => false));
}

function routeSource(address, {socket = () => dgram.createSocket('udp4'), timeout = 1000} = {}) {
  if (!net.isIPv4(address)) return Promise.reject(Error('Expected an IPv4 route destination'));
  return new Promise((resolve, reject) => {
    const probe = socket();
    let settled = false;
    const finish = (error, value) => {
      if (settled) return;
      settled = true;
      clearTimeout(timer);
      try {probe.close();} catch {}
      error ? reject(error) : resolve(value);
    };
    const timer = setTimeout(() => finish(Error('Local route lookup timed out')), timeout);
    probe.once('error', error => finish(error));
    try {
      probe.connect(9, address, () => {
        const source = probe.address().address;
        finish(net.isIPv4(source) && source !== '0.0.0.0' ? null : Error('No IPv4 route'), source);
      });
    } catch (error) {finish(error);}
  });
}

async function routeTo(address, {interfaces = os.networkInterfaces, source = routeSource, tunnel = isTunnel} = {}) {
  const localAddress = await source(address);
  for (const [name, values] of Object.entries(interfaces())) {
    const local = values?.find(value => !value.internal && value.address === localAddress);
    if (local) {
      if (await tunnel(name)) throw Error('The console route uses a VPN adapter (' + name + '). Connect the PC to the console LAN.');
      return {dev: name, address: localAddress, netmask: local.netmask};
    }
  }
  throw Error('No local network adapter for this destination');
}

module.exports = {isWireless, isTunnel, routeSource, routeTo, windowsAdapterModes};
