'use strict';
const fs = require('node:fs/promises'), path = require('node:path');
const legacy = require('./legacy-core.json');
async function retireLink(root) {
  let status;
  try {
    const response = await fetch('http://127.0.0.1:39525/status', {signal:AbortSignal.timeout(1500)});
    if (!response.ok) return;
    status = await response.json();
  } catch (error) {
    if (error.cause?.code === 'ECONNREFUSED') return;
    throw Error('Cannot verify PC Link before migrating IZ2S state. Close the previous PC Link and retry.', {cause:error});
  }
  const service = status?.service;
  if (service?.root !== root) return;
  if (service.namingVersion === 2) return;
  if (status.queued || status.fileTransfers || ['waiting','transferring','verifying','staged','applying'].includes(status.menu?.state) ||
      Object.values(status.media || {}).some(item => ['starting','running','retrying'].includes(item?.phase)))
    throw Error('Finish the active PC Link operation before updating IZ2S.');
  const pid = service.pid;
  if (!Number.isSafeInteger(pid) || pid <= 1 || process.platform !== 'linux')
    throw Error('Close the previous PC Link before migrating IZ2S state.');
  const expected = path.join(root, 'engines', legacy.engineId, 'tools/pc-link.cjs');
  const args = (await fs.readFile('/proc/' + pid + '/cmdline','utf8')).split('\0');
  const cwd = await fs.realpath('/proc/' + pid + '/cwd');
  if (!args.some(arg => arg && path.resolve(cwd,arg) === expected))
    throw Error('PC Link process ownership could not be verified.');
  const {processIdentity, processAlive} = require('../session/panel-handover.cjs');
  const identity = await processIdentity(pid);
  if (!identity) return;
  process.kill(pid,'SIGTERM');
  for (let i=0; i<50; i++) {
    if (!await processAlive(pid) || (await processIdentity(pid))?.start !== identity.start) return;
    await new Promise(resolve=>setTimeout(resolve,100));
  }
  throw Error('Previous PC Link is still shutting down. IZ2S state has not been moved.');
}
module.exports = {retireLink};
