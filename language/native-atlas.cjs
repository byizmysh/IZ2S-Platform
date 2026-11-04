'use strict';
const path = require('node:path');
const {spawn} = require('node:child_process');
const cache = new Map();
async function localize(bytes, {kind, shell, language} = {}) {
  const selected = language || (await require('./node.cjs').current()).id;
  if (!['en', 'ru', 'es', 'zh'].includes(selected)) throw Error('Unsupported interface language');
  if (!['focus', 'legacy'].includes(kind) || !['izlauncher', 'izpslauncher'].includes(shell)) throw Error('Invalid interface atlas');
  // English still goes through the renderer when original labels contain Russian.
  const key = selected + ':' + kind + ':' + shell + ':' + require('node:crypto').createHash('sha256').update(bytes).digest('hex');
  if (!cache.has(key)) {
    const pending = new Promise((resolve, reject) => {
      const child = spawn('python3', [path.join(__dirname, 'native-atlas.py'), kind, shell, selected], {stdio: ['pipe', 'pipe', 'pipe']});
      const chunks = [], errors = []; let size = 0;
      const timer = setTimeout(() => {child.kill(); reject(Error('Interface atlas translation timed out'));}, 15000);
      child.stdout.on('data', data => {size += data.length; if (size > bytes.length) child.kill(); else chunks.push(data);});
      child.stderr.on('data', data => errors.push(data));
      child.once('error', error => {clearTimeout(timer); reject(error);});
      child.once('close', code => {clearTimeout(timer); const output = Buffer.concat(chunks); if (code !== 0 || output.length !== bytes.length) reject(Error('Interface atlas translation failed: ' + Buffer.concat(errors).toString().slice(0, 500))); else resolve(output);});
      child.stdin.on('error', () => {}); child.stdin.end(bytes);
    });
    cache.set(key, pending); pending.catch(() => cache.delete(key));
  }
  return Buffer.from(await cache.get(key));
}
module.exports = {localize};
