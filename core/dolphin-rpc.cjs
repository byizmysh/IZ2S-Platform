const fs = require('node:fs/promises');
const path = require('node:path');
const {spawn} = require('node:child_process');

async function open(root, request = {}, {launch = spawn,
  findSession = require('../tools/dolphin-fs-rpc/library.cjs').findSession} = {}) {
  root = path.resolve(root);
  const session = await findSession(root);
  if (request.attemptId && request.attemptId !== session.id) throw Error('The session changed. Open Dolphin again.');
  if (!/^[a-zA-Z0-9-]{1,80}$/.test(session.id || '')) throw Error('Invalid active attempt');
  let claim;
  if (request.automatic === true) {
    const directory = path.join(root, 'data/state/dolphin-fs-rpc');
    await fs.mkdir(directory, {recursive: true});
    claim = path.join(directory, 'auto-' + session.id + '.json');
    try {
      const file = await fs.open(claim, 'wx', 0o600);
      await file.writeFile(JSON.stringify({state: 'opening', at: new Date().toISOString()}));
      await file.close();
    } catch (error) {
      if (error.code === 'EEXIST') return {ok: true, skipped: true, attemptId: session.id};
      throw error;
    }
  }
  try {
    await fs.access(path.join(root, 'tools/dolphin-fs-rpc/choose.py'));
    await new Promise((resolve, reject) => {
      const child = launch('/usr/bin/python3', [path.join(root, 'tools/dolphin-fs-rpc/choose.py')],
        {cwd: root, detached: true, stdio: 'ignore'});
      child.once('error', reject);
      child.once('spawn', () => {child.unref(); resolve();});
    });
    if (claim) await fs.writeFile(claim, JSON.stringify({state: 'opened', at: new Date().toISOString()}), {mode: 0o600});
    return {ok: true, attemptId: session.id};
  } catch (error) {
    if (claim) await fs.writeFile(claim, JSON.stringify({state: 'failed', error: error.message}), {mode: 0o600});
    throw error;
  }
}
async function onSessionReady(root, row, {preview = false, openChooser = open} = {}) {
  if (preview || row?.status !== 'running' || !row.runDirectory) return {ok: true, skipped: true};
  let state;
  try {state = JSON.parse(await fs.readFile(path.join(row.runDirectory, 'fs-rpc-state.json'), 'utf8'));}
  catch (error) {if (error.code === 'ENOENT') return {ok: true, skipped: true}; throw error;}
  if (state.state !== 'ready') return {ok: true, skipped: true};
  return openChooser(root, {automatic: true, attemptId: row.id});
}
module.exports = {open, onSessionReady};
