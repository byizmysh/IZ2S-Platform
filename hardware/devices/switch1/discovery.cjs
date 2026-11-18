'use strict';
// Existing keyboards expose TCP only. Search a bounded directly attached LAN,
// then let pairing.cjs authenticate candidates; an open port is never identity.
const net = require('node:net'), os = require('node:os');
const {privateIP} = require('../../../core/session/network-discovery.cjs');
const {isTunnel} = require('../../../core/platform/network.cjs');
const number = ip => ip.split('.').reduce((n, part) => (n * 256 + Number(part)) >>> 0, 0);
const dotted = n => [24,16,8,0].map(shift => (n >>> shift) & 255).join('.');
async function addresses({interfaces = os.networkInterfaces, tunnel = isTunnel} = {}) {
 const hosts = [], own = new Set();
 for (const [name, rows] of Object.entries(interfaces())) {
  for (const row of rows || []) own.add(row.address);
  if (/^(docker|podman|veth|virbr|vnet|vmnet|lxc|br-)/i.test(name) || await tunnel(name)) continue;
  for (const row of rows || []) if (!row.internal && (row.family === 'IPv4' || row.family === 4) && privateIP(row.address) && net.isIPv4(row.netmask)) hosts.push(row);
 }
 const result = new Set();
 for (const host of hosts.slice(0, 4)) {
  const ip = number(host.address), mask = number(host.netmask), size = (~mask) >>> 0;
  // No automatic traversal of large company networks or point-to-point links.
  if (size < 3 || size > 255 || (size & (size + 1)) !== 0) continue;
  const first = (ip & mask) >>> 0;
  for (let offset = 1; offset < size && result.size < 512; offset++) {
   const address = dotted(first + offset); if (!own.has(address)) result.add(address);
  }
 }
 return [...result];
}
function probe(address, port, timeout = 250) {
 return new Promise(resolve => {
  let socket, done = false;
  const finish = ready => {if (done) return;done = true;clearTimeout(timer);socket?.destroy();resolve(ready);};
  const timer = setTimeout(() => finish(false), timeout);
  try {socket = net.createConnection({host:address,port});socket.once('connect', () => finish(true));socket.once('error', () => finish(false));socket.once('close', () => finish(false));}
  catch {finish(false);}
 });
}
async function discover({port = 24872, list = addresses, check = probe, exclude = []} = {}) {
 if (!Number.isInteger(port) || port < 1024 || port > 65535) return [];
 const targets = (await list()).filter(ip => privateIP(ip) && !exclude.includes(ip)).slice(0,512), found = [];
 let cursor = 0;
 await Promise.all(Array.from({length:Math.min(24, targets.length)}, async () => {
  while(cursor < targets.length) {const ip = targets[cursor++];if (await check(ip,port)) found.push(ip);}
 }));
 // Ambiguous/hostile networks must not silently truncate into a unique match.
 return found.length > 16 ? [] : found.sort();
}
module.exports = {addresses, probe, discover};
