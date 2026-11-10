#!/usr/bin/env node
'use strict';
// Noninteractive panel entry point. Upstream run.js remains available unchanged.
const fs = require('node:fs/promises'), path = require('node:path');
const {spawn} = require('node:child_process');
async function inject({root, phy, hostMac = '', chatDelay = 10, handshakeScale = 1}) {
  const p = require('./integration.cjs').paths(root);
  require('./integration.cjs').validateKeys(await fs.readFile(p.keys, 'utf8'));
  if (!/^phy\d+$/.test(phy) || hostMac && !/^(?:[a-f0-9]{2}:){5}[a-f0-9]{2}$/i.test(hostMac)) throw Error('Invalid LDN radio or host');
  if (!Number.isInteger(chatDelay) || chatDelay < 10 || chatDelay > 5000 || !Number.isFinite(handshakeScale) || handshakeScale < 1 || handshakeScale > 10) throw Error('Invalid LDN timing');
  const lines = await require('./compress.js')(await fs.readFile(path.join(p.folder, 'payloads/core.js'), 'utf8'));
  console.log('CIV7_LDN_PAYLOAD_LINES=' + lines.length);
  const args = [path.join(p.folder, 'bash/network-panel.sh'), '--phy', phy, p.helper,
    p.helper, '-u', path.join(p.folder, 'ldn/main.py'), '--keys', p.keys, '--phy', phy,
    '--ifname', 'iz2s-civ7-ldn', '--chat-delay', String(chatDelay), '--handshake-scale', String(handshakeScale)];
  if (hostMac) args.push('--host', hostMac);
  await new Promise((resolve, reject) => {
    const child = spawn('bash', args, {cwd: p.folder, stdio: ['pipe', 'inherit', 'inherit']});
    const interrupt = () => child.kill('SIGTERM');
    process.on('SIGINT', interrupt); process.on('SIGTERM', interrupt);
    const cleanup = () => {process.off('SIGINT', interrupt); process.off('SIGTERM', interrupt);};
    child.on('error', error => {cleanup(); reject(error);});
    child.on('close', (code, signal) => {cleanup(); code === 0 ? resolve() : reject(Error('LDN delivery stopped: ' + (signal || code)));});
    child.stdin.on('error', error => {if (error.code !== 'EPIPE') console.error('LDN input pipe failed');});
    child.stdin.end(lines.join('\n') + '\n');
  });
  console.log('CIV7_LDN_DELIVERY_COMPLETE');
}
if (require.main === module) {
  const [phy, hostMac, delay, scale] = process.argv.slice(2);
  inject({root: path.resolve(__dirname, '../..'), phy, hostMac, chatDelay: Number(delay), handshakeScale: Number(scale)})
    .catch(error => {console.error(error.message); process.exitCode = 1;});
}
module.exports = {inject};
