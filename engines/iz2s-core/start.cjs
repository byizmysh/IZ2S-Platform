#!/usr/bin/env node
'use strict';
const fs = require('node:fs/promises');
const path = require('node:path');
const net = require('node:net');
const { spawn } = require('node:child_process');
const runtime = require('./runtime.cjs');
async function main() {
  const args = process.argv.slice(2);
  if (args.length === 1 && args[0] === '--check') {
    console.log(JSON.stringify(await runtime.verify(), null, 2));
    return;
  }
  if (args.length !== 1 || !net.isIPv4(args[0]))
    throw Error('Usage: node engines/iz2s-core/start.cjs PC-LAN-IP | --check');
  const host = args[0];
  await runtime.verify();
  const out = runtime.output();
  await fs.mkdir(runtime.runs(out), { recursive: true });
  await fs.mkdir(path.join(out, 'tmp'), { recursive: true });
  await new Promise((resolve, reject) => {
    const probe = net.createServer();
    probe.once('error', error => reject(Error('Port 39380 is unavailable: ' + error.code)));
    probe.listen(39380, host, () => probe.close(resolve));
  });
  console.log('IZ2S core · 0.25 · SD root /iz2s/core');
  console.log('Use a fresh SmileBASIC Direct session. Existing compatible saved bootstrap: LOAD "TXT:NS2IZM".');
  console.log('Do not close SmileBASIC before IZ2S launch completed and the IZ2S PID are printed.');
  const child = spawn(process.execPath, [path.join(runtime.directory, 'installer/core.cjs'), '--dir',
    path.join(runtime.directory, 'package'), host], {
    cwd: path.join(runtime.directory, 'installer'),
    env: { ...process.env, NS2_BASIC_ARITY: '5', NS2_RUNS_DIR: runtime.runs(out), TMPDIR: path.join(out, 'tmp') },
    stdio: 'inherit',
  });
  child.once('error', error => { console.error(error); process.exitCode = 1; });
  child.once('exit', (code, signal) => { process.exitCode = code ?? (signal ? 130 : 1); });
  for (const signal of ['SIGINT', 'SIGTERM']) process.on(signal, () => child.kill(signal));
}
main().catch(error => { console.error(error.message); process.exitCode = 1; });
