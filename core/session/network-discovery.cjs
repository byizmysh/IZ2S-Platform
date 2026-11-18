const dgram = require('node:dgram');
const crypto = require('node:crypto');
const { isIPv4 } = require('node:net');
const PORT = 39418,
  GROUP = '239.255.77.14',
  PROTOCOL = 'izlauncher.discovery';
const number = (ip) =>
  ip.split('.').reduce((v, n) => (v * 256 + Number(n)) >>> 0, 0);
const dotted = (v) => [24, 16, 8, 0].map((n) => (v >>> n) & 255).join('.');
function privateIP(ip) {
  if (!isIPv4(ip)) return false;
  const [a, b] = ip.split('.').map(Number);
  return (
    a === 10 || (a === 192 && b === 168) || (a === 172 && b >= 16 && b <= 31)
  );
}
function sameSubnet(a, b, mask) {
  return (
    isIPv4(a) &&
    isIPv4(b) &&
    isIPv4(mask) &&
    (number(a) & number(mask)) === (number(b) & number(mask))
  );
}
function broadcast(host) {
  if (!isIPv4(host.netmask) || number(host.netmask) >= 0xfffffffe) return null;
  return dotted((number(host.address) | ~number(host.netmask)) >>> 0);
}
function response(bytes, remote, nonce, hosts) {
  if (bytes.length > 4096 || !privateIP(remote.address)) return null;
  try {
    const value = JSON.parse(bytes);
    if (
      value.protocol !== PROTOCOL ||
      value.version !== 1 ||
      value.nonce !== nonce ||
      value.kind !== 'input' ||
      !/^[a-f0-9]{32}$/.test(value.id) ||
      !Array.isArray(value.capabilities) ||
      !value.capabilities.includes('usb-keyboard') ||
      !hosts.some((h) => sameSubnet(remote.address, h.address, h.netmask))
    )
      return null;
    const iface =
      Array.isArray(value.interfaces) &&
      value.interfaces.find((i) => i.address === remote.address);
    return {
      id: value.id,
      name: String(value.name || 'ARM PC')
        .replace(/[\x00-\x1f]/g, '')
        .slice(0, 64),
      kind: 'input',
      address: remote.address,
      mode: ['lan', 'wifi'].includes(iface?.mode) ? iface.mode : 'unknown',
      architecture: String(value.architecture || '').slice(0, 24),
      source: 'discovery',
    };
  } catch {
    return null;
  }
}
// Metadata only. HID jobs and SSH commands never travel over this UDP service.
async function discover({
  hosts,
  targets = [],
  timeout = 650,
  port = PORT,
  group = GROUP,
}) {
  const nonce = crypto.randomBytes(16).toString('hex');
  const query = Buffer.from(
    JSON.stringify({ protocol: PROTOCOL, version: 1, kind: 'discover', nonce }),
  );
  const peers = new Map();
  const errors = [];
  await Promise.all(
    hosts
      .filter((h) => privateIP(h.address))
      .slice(0, 8)
      .map(
        (host) =>
          new Promise((resolve) => {
            const socket = dgram.createSocket('udp4');
            let done = false;
            const finish = () => {
              if (done) return;
              done = true;
              clearTimeout(timer);
              try {
                socket.close();
              } catch {}
              resolve();
            };
            const timer = setTimeout(finish, timeout);
            socket.on('error', (e) => {
              errors.push(e.code || 'Discovery unavailable');
              finish();
            });
            socket.on('message', (bytes, remote) => {
              const peer = response(bytes, remote, nonce, hosts);
              if (
                peer &&
                !hosts.some((h) => h.address === peer.address) &&
                peers.size < 32
              )
                peers.set(peer.address, peer);
            });
            socket.bind(0, host.address, () => {
              if (done) return;
              try {
                socket.setBroadcast(true);
                socket.setMulticastTTL(1);
                socket.setMulticastInterface(host.address);
              } catch (e) {
                errors.push(e.code || 'Discovery unavailable');
              }
              const destinations = [
                ...new Set(
                  [
                    group,
                    broadcast(host),
                    ...targets.filter(
                      (ip) =>
                        privateIP(ip) &&
                        sameSubnet(ip, host.address, host.netmask),
                    ),
                  ].filter(Boolean),
                ),
              ];
              for (const ip of destinations)
                socket.send(query, port, ip, (e) => {
                  if (e) errors.push(e.code);
                });
            });
          }),
      ),
  );
  return { peers: [...peers.values()], errors: [...new Set(errors)] };
}
module.exports = {
  discover,
  response,
  sameSubnet,
  broadcast,
  privateIP,
  PORT,
  GROUP,
  PROTOCOL,
};
