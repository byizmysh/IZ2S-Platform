'use strict';
const fs = require('node:fs/promises');
const path = require('node:path');
const crypto = require('node:crypto');
const migration = require('../../core/platform/core-migration.cjs');
migration.configureEnvironment();
const directory = path.join(__dirname, 'runtime');
// Runtime state and temporary payloads are separate from the platform log tree.
const DEFAULT_OUT = path.resolve(__dirname, '../../data/core');
const output = value => path.resolve(value || process.env.IZ2S_CORE_OUT || DEFAULT_OUT);
const logs = out => output(out) === DEFAULT_OUT
  ? path.resolve(__dirname, '../../data/log') : path.join(output(out), 'log');
const runs = out => path.join(logs(out), 'runs');
const journal = out => path.join(logs(out), 'iz2s-core');
function logAliases(out) {
  return [[path.join(output(out), 'logs/iz2s-core'), '/data/log/iz2s-core'],
    [path.join(output(out), 'logs/pc-link'), '/data/log/pc-link'],
    [logs(out), '/data/log']];
}
function displayPath(text, out) {
  return logAliases(out).reduce((value, [from, to]) => value.replaceAll(from, to), migration.displayText(text));
}
function displayedBytes(bytes, out) {
  // Timing boundaries may split a UTF-8 character. Preserve byte positions,
  // including partial characters, while shortening the known path prefixes.
  return logAliases(out).reduce((value, [from, to]) =>
    value.replaceAll(Buffer.from(from).toString('latin1'), to), migration.displayText(bytes.toString('latin1'))).length;
}
async function verify() {
  const manifest = JSON.parse(await fs.readFile(path.join(directory, 'manifest.json'), 'utf8'));
  let bytes = 0;
  for (const entry of manifest.files) {
    const file = path.resolve(directory, entry.path);
    if (!file.startsWith(directory + path.sep)) throw Error('Invalid IZ2S runtime path: ' + entry.path);
    const data = await fs.readFile(file);
    if (data.length !== entry.bytes || crypto.createHash('sha256').update(data).digest('hex') !== entry.sha256)
      throw Error('IZ2S runtime integrity check failed: ' + entry.path);
    bytes += data.length;
  }
  return { files: manifest.files.length, bytes, directory };
}
module.exports = { directory, DEFAULT_OUT, output, logs, runs, journal, displayPath, displayedBytes, verify,
  start: path.join(__dirname, 'start.cjs'), bootstrap: path.join(directory, 'bootstrap.bas') };
