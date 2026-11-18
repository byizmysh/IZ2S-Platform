'use strict';
const fs = require('node:fs/promises');
const path = require('node:path');

async function connectedToThisPc({ platform = process.platform, directory = '/sys/bus/usb/devices' } = {}) {
  if (platform !== 'linux') return false;
  const entries = await fs.readdir(directory).catch(() => []);
  for (const entry of entries) {
    const device = path.join(directory, entry);
    try {
      const product = await fs.readFile(path.join(device, 'product'), 'utf8');
      if (product.trim() !== 'NXHID Wi-Fi Keyboard') continue;
      const manufacturer = await fs.readFile(path.join(device, 'manufacturer'), 'utf8');
      if (manufacturer.trim() === 'NXHID') return true;
    } catch { /* The device may unplug or disappear during enumeration. */ }
  }
  return false;
}

module.exports = { connectedToThisPc };
