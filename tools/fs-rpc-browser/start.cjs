#!/usr/bin/env node
const fs = require('node:fs/promises'), path = require('node:path');
const {spawn} = require('node:child_process');
const {randomUUID} = require('node:crypto');
const root = path.resolve(__dirname, '../..');
const {readJson, saveJson} = require('../../core/session/ledger.cjs');
const windowFile = path.join(root, 'data/state/fs-rpc-browser/window.json');
async function claimLock(lock) {
  // Publish a complete PID atomically. A crash between open('wx') and write
  // must not leave an empty lock that blocks every later start.
  const candidate = lock + '.' + process.pid + '-' + randomUUID();
  try {
    await fs.writeFile(candidate, String(process.pid), {flag:'wx', mode:0o600});
    try {await fs.link(candidate, lock);}
    catch (error) {
      // Portable RUN folders may live on exFAT/FAT without hard links.
      if (!['ENOTSUP','EOPNOTSUPP','EPERM','EXDEV','ENOSYS'].includes(error.code)) throw error;
      const handle = await fs.open(lock, 'wx', 0o600);
      try {await handle.writeFile(String(process.pid));}
      catch (error) {await fs.rm(lock, {force:true}); throw error;}
      finally {await handle.close();}
    }
    return true;
  } catch (error) {if (error.code === 'EEXIST') return false; throw error;}
  finally {await fs.rm(candidate, {force:true});}
}
async function running() {
  const previous = await readJson(windowFile, {});
  try {
    const url = new URL(previous.url);
    if (url.protocol !== 'http:' || url.hostname !== '127.0.0.1' || url.pathname !== '/' || !/^#[a-f0-9]{64}$/.test(url.hash)) return null;
    const response = await fetch(url.origin + '/api/settings', {headers: {'x-fs-key': url.hash.slice(1)}, signal: AbortSignal.timeout(1500)});
    const settings=await response.json();
    if (!response.ok || settings.application !== 'iz2s-files') return null;
    return {...previous,legacy:settings.workspaceTabs!==1};
  } catch {return null;}
}
async function main(args = process.argv.slice(2)) {
  if (args.some(arg => arg !== '--no-open')) throw Error('Usage: node tools/fs-rpc-browser/start.cjs [--no-open]');
  require('./requirements.cjs').check();
  let window = await running(), app;
  let legacyInterfaces=window?.legacy?[...(window.legacyInterfaces||[]),{pid:window.pid,url:window.url}]:[];
  if(window?.legacy)window=null;
  if (!window) {
    await fs.mkdir(path.dirname(windowFile), {recursive: true, mode: 0o700});
    const lock = windowFile + '.lock';
    let owned = false;
    const deadline = Date.now() + 6000;
    while (!owned && !window && Date.now() < deadline) {
      owned = await claimLock(lock);
      if (!owned) {
        window = await running();
        if(window?.legacy)window=null;
        if (window) break;
        const owner = Number(await fs.readFile(lock, 'utf8').catch(() => '0'));
        if (Number.isInteger(owner) && owner > 1) {
          try {process.kill(owner, 0);} catch (e) {if (e.code === 'ESRCH') await fs.rm(lock, {force: true}); else throw e;}
        } else {
          const stat = await fs.stat(lock).catch(() => null);
          if (stat && Date.now() - stat.mtimeMs > 30000) await fs.rm(lock, {force:true});
        }
        await new Promise(resolve => setTimeout(resolve, 100));
      }
    }
    if (!window && !owned) throw Error('The Super File Bros. page is still starting. Try again shortly.');
    if (owned) {
      try {
        window = await running();
        if(window?.legacy){legacyInterfaces=[...(window.legacyInterfaces||[]),{pid:window.pid,url:window.url}];window=null;}
        if (!window) {
          app = await require('./server.cjs').create({legacyInterfaces});
          window = {pid: process.pid, url: app.url,legacyInterfaces};
          await saveJson(windowFile, window);
        }
      } finally {await fs.rm(lock, {force: true});}
    }
  }
  if (!args.includes('--no-open')) {
    const child = spawn(process.platform === 'darwin' ? 'open' : 'xdg-open', [window.url], {detached: true, stdio: 'ignore'});
    await new Promise((resolve, reject) => {
      let settled=false;
      const finish=error=>{if(settled)return;settled=true;clearTimeout(timer);child.unref();error?reject(error):resolve();};
      const timer=setTimeout(()=>finish(),2000);
      child.once('error',finish);
      child.once('close',code=>finish(code===0?null:Error('The browser could not open the Super File Bros. page. Check the default browser.')));
    });
  }
  if (app) {
    let closing;
    const close = () => closing ||= (async () => {app.server.closeAllConnections(); await app.close();})();
    process.once('SIGINT', () => close().catch(console.error));
    process.once('SIGTERM', () => close().catch(console.error));
  }
  return {url: window.url, pid:window.pid, reused: !app};
}
if (require.main === module) main().then(result => {
  if (process.connected) process.send({type:'rpc-browser-opened',url:result.url,pid:result.pid}, () => process.disconnect());
}).catch(error => {console.error(error.message); process.exitCode = 1; if (process.connected) process.disconnect();});
module.exports = {main, running, claimLock};
