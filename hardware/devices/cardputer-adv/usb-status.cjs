'use strict';
const fs = require('node:fs/promises'), path = require('node:path');
const normalizedSerial = value => typeof value === 'string' ? value.replace(/[^a-z0-9]/gi, '').toLowerCase() : '';

// Optional local observation. An unavailable OS inventory never identifies a host.
async function localHost(serial, {platform = process.platform, directory = '/sys/bus/usb/devices'} = {}) {
  const wanted = normalizedSerial(serial);
  if (platform !== 'linux' || wanted.length < 8 || wanted.length > 64) return 'unknown';
  let entries;
  try {entries = await fs.readdir(directory);} catch {return 'unknown';}
  for (const entry of entries.slice(0, 256)) {
    try {
      const device = path.join(directory, entry);
      if (normalizedSerial((await fs.readFile(path.join(device, 'serial'), 'utf8')).trim()) !== wanted) continue;
      const product = (await fs.readFile(path.join(device, 'product'), 'utf8')).trim();
      const manufacturer = (await fs.readFile(path.join(device, 'manufacturer'), 'utf8')).trim();
      if (product === 'IZ2S Cardputer ADV Keyboard' && manufacturer === 'IZMYSH') return 'this-pc';
    } catch { /* USB devices can disappear during enumeration. */ }
  }
  return 'unknown';
}

function describe(state, host = 'unknown') {
  const configured = typeof state.usbConfigured === 'boolean' ? state.usbConfigured : state.keyboard === true || state.usbState === 'configured';
  const suspended = state.usbSuspended === true || state.usbState === 'suspended';
  const recovering = state.usbRecovering === true || state.usbState === 'reconnecting';
  const connected = state.usbConnected === true || configured || suspended || state.usbState === 'attached';
  const compatible = state.usbKeyboardFormat === 'boot-8-byte';
  const keyboard = compatible && state.keyboard === true && configured && !suspended && !recovering && host !== 'this-pc';
  const detail = !compatible ? 'Update ADV firmware from First Setup for Switch keyboard compatibility.'
    : host === 'this-pc' ? 'ADV USB is connected to this PC. Move its USB data cable to Switch 2.'
    : recovering ? 'Reconnecting USB keyboard. No commands are replayed.'
    : suspended ? 'USB inactive. Wake the console or check the cable.'
    : keyboard ? 'USB connected · keyboard ready.'
    : connected ? 'USB connected; waiting for keyboard configuration.'
    : 'USB host not responding. Check the dock, cable and console.';
  return {connected, configured, suspended, recovering, keyboard, localHost: host, detail,
    state: recovering ? 'reconnecting' : suspended ? 'suspended' : configured ? 'configured' : connected ? 'attached' : 'not-attached',
    generation: Number.isSafeInteger(state.usbGeneration) && state.usbGeneration >= 0 ? state.usbGeneration : null};
}
module.exports = {describe, localHost};
