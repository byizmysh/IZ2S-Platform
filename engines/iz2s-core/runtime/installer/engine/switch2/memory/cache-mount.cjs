// Use the application's verified SDK mount lifecycle; never close borrowed handles.
const fs = require('node:fs/promises');
const path = require('node:path');
const {execFile} = require('node:child_process');
const {promisify} = require('node:util');
const run = promisify(execFile);
const bindings = require("./cache-mount-sdk.json").functions;
const {check} = require("./session.cjs");

async function refresh(directory) {
  await run(process.execPath, [path.join(directory, 'inspect-mounts.cjs')], {timeout: 30000});
}

async function verify(s) {
  await s.verifyCode(bindings.Unmount, 'SDK unmount');
  await s.verifyCode(bindings.MountCache, 'SDK cache mount');
}

async function unmount(s, live, directory) {
  const mounts = JSON.parse(await fs.readFile(path.join(directory, 'mounts.json')));
  const mount = mounts.mounts.find(m => m.name === 'tmp');
  check(mount, 'The application cache mount is missing');
  const bytes = await s.read(mount.accessor, 128);
  check(bytes.subarray(24, 40).toString().split('\0')[0] === 'tmp', 'Mount name changed');
  for (const offset of [0x30, 0x40]) {
    const head = BigInt(mount.accessor + offset);
    check(bytes.readBigUInt64LE(offset) === head && bytes.readBigUInt64LE(offset + 8) === head,
      'Application cache has open SDK files or directories');
  }
  const name = s.base + 0x5200;
  await s.write(name, Buffer.from('tmp\0'));
  const result = await s.rpc.call3(live.sdk + bindings.Unmount.offset, name, 0, 0);
  await s.record({operation: 'Unmount application cache through SDK', result});
  check(!result, 'SDK unmount failed');
}

async function mount(s, live, directory) {
  const name = s.base + 0x5200;
  await s.write(name, Buffer.from('tmp\0'));
  const result = await s.rpc.call3(live.sdk + bindings.MountCache.offset, name, 0, 0);
  await s.record({operation: 'Restore application cache through SDK', result});
  check(!result, 'SDK cache mount failed');
  await refresh(directory);
}

function openInput() {
  const input = Buffer.alloc(0x48);
  input[0] = 1;
  input[0x28] = 5;
  return input;
}

module.exports = {verify, unmount, mount, refresh, openInput};
