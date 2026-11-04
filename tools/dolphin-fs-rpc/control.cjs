// One per-user game export, shared by the Dolphin chooser and console app.
const net = require('node:net');
const path = require('node:path');
const crypto = require('node:crypto');
const {spawn} = require('node:child_process');
const {ROOT} = require('./library.cjs');
const uid = process.getuid?.() ?? require('node:os').userInfo().username;
const runtimeDirectory = '/run/user/' + uid;
const name = 'ns2-fs-' + crypto.createHash('sha256').update(ROOT + ':' + uid + ':' + (process.env.NS2_FS_BROWSER === '1' ? 'standalone' : 'panel')).digest('hex').slice(0, 24);
const socketPath = process.platform === 'win32' ? '\\\\.\\pipe\\' + name :
  path.join(require('node:fs').existsSync(runtimeDirectory) ? runtimeDirectory : require('node:os').tmpdir(), name + '.sock');
function requestRaw(method, values = {}, timeout = 120000) {
  return new Promise((resolve, reject) => {
    const socket = net.createConnection(socketPath);
    let input = '';
    socket.setEncoding('utf8');
    socket.setTimeout(timeout, () => socket.destroy(Error('Filesystem service is busy or not responding. Wait for the current operation, then retry.')));
    socket.once('error', reject);
    socket.once('connect', () => socket.write(JSON.stringify({method, ...values}) + '\n'));
    socket.on('data', bytes => {input += bytes; if (input.length > 32 * 1024 * 1024) socket.destroy(Error('Game service reply is too large'));});
    socket.once('end', () => {
      try {const result = JSON.parse(input); if (result.error && !result.status) throw Error(result.error); resolve(result);}
      catch (error) {reject(error);}
    });
  });
}
let launching;
async function ensure() {
  try {await requestRaw('status', {}, 1000); return;} catch (error) {
    if (!['ENOENT', 'ECONNREFUSED'].includes(error.code)) throw error;
  }
  if (!launching) launching = (async () => {
    await new Promise((resolve, reject) => {
      const child = spawn(process.execPath, [path.join(__dirname, 'service.cjs')], {cwd: ROOT, detached: true, stdio: 'ignore'});
      child.once('error', reject); child.once('spawn', () => {child.unref(); resolve();});
    });
    for (let i = 0; i < 50; i++) {
      try {await requestRaw('status', {}, 1000); return;} catch (error) {
        if (!['ENOENT', 'ECONNREFUSED'].includes(error.code)) throw error;
      }
      await new Promise(resolve => setTimeout(resolve, 100));
    }
    throw Error('Could not start the shared game service');
  })().finally(() => {launching = null;});
  await launching;
}
async function request(method, values) {await ensure(); return requestRaw(method, values);}
module.exports = {socketPath, requestRaw, request};
