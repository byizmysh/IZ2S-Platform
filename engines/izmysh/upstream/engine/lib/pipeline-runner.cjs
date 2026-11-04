'use strict';
const fs = require('node:fs'), files = require('node:fs/promises'), path = require('node:path');
const {spawn} = require('node:child_process');
async function startEngine(directory, dumpDirectory = null, held = false) {
  const log = await files.open(path.join(directory, 'run.log'), 'wx', 0o600);
  const output = fs.createWriteStream(null, {fd:log.fd, autoClose:false});
  const flags = [...(dumpDirectory ? ['--dump-firmware'] : []), ...(held ? ['--hold-rpc'] : [])];
  const child = spawn(process.execPath, [path.join(directory,'switch2/pipeline.cjs'), ...flags], {
    cwd:directory, windowsHide:true, stdio:['ignore','pipe','pipe', ...(held ? ['ipc'] : [])],
    env:{...process.env, ...(dumpDirectory ? {NS2_FIRMWARE_DUMP_DIR:dumpDirectory} : {})},
  });
  let error, pending = '', readyResolve, readyReject, releaseTask;
  const ready = held ? new Promise((resolve, reject) => {readyResolve = resolve; readyReject = reject;}) : null;
  ready?.catch(() => {});
  child.stdout.on('data', bytes => {
    pending = (pending + bytes).slice(-65536);
    let end;
    while ((end = pending.indexOf('\n')) >= 0) {
      const line = pending.slice(0,end).replace(/\r$/, ''); pending = pending.slice(end+1);
      if (/^[1-4]\/4 |^Firmware: /.test(line)) console.log(line);
    }
  });
  child.stdout.pipe(output, {end:false}); child.stderr.pipe(output, {end:false});
  child.once('error', value => {error = value;});
  output.once('error', value => {error = value; child.kill('SIGTERM');});
  const receive = message => {
    if (message?.type === 'held-ready') {child.off('message', receive); readyResolve?.(message);}
  };
  if (held) child.on('message', receive);
  const finished = new Promise(resolve => child.once('close', (code, signal) => resolve({code,signal})))
    .then(async outcome => {
      child.off('message', receive);
      readyReject?.(error || Error('The FS endpoint exited before the held session was ready.'));
      try {
        if (!output.destroyed) await new Promise((resolve,reject) => {output.once('error',reject); output.end(resolve);});
      } catch (value) {error ||= value;}
      finally {await log.close();}
      return {...outcome, error};
    });
  return {child, ready, finished, release() {
    if (!releaseTask) releaseTask = (async () => {
      if (held && child.connected) await new Promise((resolve, reject) => {
        child.send({type:'release'}, error => error ? reject(Error('FS release IPC failed; outcome unknown')) : resolve());
      });
      return finished;
    })();
    return releaseTask;
  }};
}
function checkEngine(outcome) {
  if (outcome.error) throw outcome.error;
  if (outcome.code !== 0) throw Error('The run stopped. Keep the runs folder and restart the console before trying again.');
}
function waitForRpcStop(connection, signals = process, engineFinished = null) {
  return new Promise(resolve => {
    const socket = connection.socket;
    let settled = false;
    const handlers = {SIGINT:() => finish('SIGINT'), SIGTERM:() => finish('SIGTERM')};
    const closed = () => finish('disconnected');
    function finish(reason) {
      if (settled) return; settled = true;
      for (const [event, handler] of Object.entries(handlers)) signals.off(event, handler);
      socket.off('close', closed); resolve(reason);
    }
    for (const [event, handler] of Object.entries(handlers)) signals.once(event, handler);
    socket.once('close', closed);
    engineFinished?.then(() => finish('engine-exited'), () => finish('engine-exited'));
    if (connection.error || socket.destroyed) finish('disconnected');
  });
}
module.exports = {startEngine, checkEngine, waitForRpcStop};
