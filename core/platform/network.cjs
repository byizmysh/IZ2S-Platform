// Host networking through Node APIs. Route selection sends no probe payload.
const os = require('node:os');
const net = require('node:net');
const dgram = require('node:dgram');
const fs = require('node:fs/promises');

async function isWireless(name, {platform = process.platform, stat = fs.stat} = {}) {
  if (/^(?:wl|wlan)|wi[ -]?fi|wireless/i.test(name)) return true;
  if (platform !== 'linux') return false;
  // Optional interface metadata; absence does not disable networking.
  return stat('/sys/class/net/' + name + '/wireless').then(() => true, () => false);
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

async function routeTo(address, {interfaces = os.networkInterfaces, source = routeSource} = {}) {
  const localAddress = await source(address);
  for (const [name, values] of Object.entries(interfaces())) {
    const local = values?.find(value => !value.internal && value.address === localAddress);
    if (local) return {dev: name, address: localAddress, netmask: local.netmask};
  }
  throw Error('No local network adapter for this destination');
}

module.exports = {isWireless, routeSource, routeTo};
