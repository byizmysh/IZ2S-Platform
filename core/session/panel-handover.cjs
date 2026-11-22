const fs = require('node:fs/promises');
const path = require('node:path');
const {execFile} = require('node:child_process');
const {promisify} = require('node:util');
async function windowsProcesses(pid) {
  const filter = pid === undefined ? '' : ' -Filter "ProcessId = ' + pid + '"';
  const script = '[Console]::OutputEncoding=[System.Text.UTF8Encoding]::new($false); Get-CimInstance Win32_Process' + filter +
    ' | Select-Object ProcessId,ParentProcessId,ExecutablePath,CommandLine,@{n="start";e={$_.CreationDate.ToUniversalTime().Ticks.ToString()}} | ConvertTo-Json -Compress';
  const {stdout} = await promisify(execFile)('powershell.exe', ['-NoProfile','-NonInteractive','-Command',script],
    {timeout:10000,maxBuffer:4*1024*1024,windowsHide:true});
  const rows = stdout.trim() ? JSON.parse(stdout.replace(/^\uFEFF/,'')) : [];
  return Array.isArray(rows) ? rows : [rows];
}
function windowsArguments(command) {
  return (command?.match(/"[^"]*"|\S+/g) || []).map(arg => arg.replace(/^"|"$/g,''));
}

async function processIdentity(pid) {
  if (process.platform === 'win32') {
    const row = (await windowsProcesses(pid))[0];
    return row ? {start:row.start,state:'S',parent:row.ParentProcessId} : null;
  }
  try {
    const stat = await fs.readFile('/proc/' + pid + '/stat', 'utf8');
    const fields = stat.slice(stat.lastIndexOf(')') + 2).trim().split(/\s+/);
    return {start: fields[19], state: fields[0], parent: Number(fields[1])};
  } catch (error) {
    if (error.code === 'ENOENT' || error.code === 'ESRCH') return null;
    throw error;
  }
}
async function processAlive(pid) {
  if (process.platform === 'linux') {
    const identity = await processIdentity(pid);
    return !!identity && !['Z', 'X'].includes(identity.state);
  }
  try { process.kill(pid, 0); return true; }
  catch (error) { if (error.code === 'ESRCH') return false; throw error; }
}

async function inspectOwner(pid) {
  if (!Number.isSafeInteger(pid) || pid <= 1 || !['linux','win32'].includes(process.platform)) return null;
  try {
    const row = process.platform === 'win32' ? (await windowsProcesses(pid))[0] : null;
    if (process.platform === 'win32' && (!row || path.basename(row.ExecutablePath || '').toLowerCase() !== 'node.exe')) return null;
    const args = row ? windowsArguments(row.CommandLine) : (await fs.readFile('/proc/' + pid + '/cmdline', 'utf8')).split('\0');
    const script = row && args.find(arg => path.isAbsolute(arg) && path.basename(arg).toLowerCase() === 'start.cjs');
    if (row && !script) return null;
    const root = await fs.realpath(row ? path.dirname(script) : '/proc/' + pid + '/cwd');
    if (!args.some(arg => arg && path.resolve(root, arg) === path.join(root, 'start.cjs'))) return null;
    const manifest = JSON.parse(await fs.readFile(path.join(root, 'package.json'), 'utf8'));
    const state = await fs.readFile(path.join(root, 'data/state/window.json'), 'utf8')
      .then(JSON.parse).catch(error => { if (error.code === 'ENOENT') return null; throw error; });
    if (manifest.name !== 'iz2s-platform' || args.includes('--preview') ||
        (state && (state.pid !== pid || state.preview))) return null;
    const edition = JSON.parse(await fs.readFile(path.join(root, 'distribution/profile.json'), 'utf8')).edition;
    if (!/^(?:RUN\d+|\d+\.\d+(?:\.\d+)?)$/.test(edition)) return null;
    const identity = await processIdentity(pid);
    if (!identity || ['Z', 'X'].includes(identity.state)) return null;
    const version=state?.version ?? JSON.parse(await fs.readFile(path.join(root,'panel/profile.json'),'utf8')).version;
    return { root, edition, version, start: identity.start };
  } catch { return null; }
}

async function ownedChildren(pid) {
  if (process.platform === 'win32') {
    const owner = await inspectOwner(pid);
    if (!owner) return [];
    const rows = await windowsProcesses(), children = [];
    const visit = parent => {
      for (const row of rows.filter(row => row.ParentProcessId === parent)) {
        // Only the verified platform's own Node workers; never terminate a browser.
        if (path.basename(row.ExecutablePath || '').toLowerCase() !== 'node.exe' ||
            !windowsArguments(row.CommandLine).some(arg => path.isAbsolute(arg) &&
              path.relative(owner.root,arg) && !path.relative(owner.root,arg).startsWith('..') &&
              !path.isAbsolute(path.relative(owner.root,arg)))) continue;
        visit(row.ProcessId); children.push({pid:row.ProcessId,start:row.start});
      }
    };
    visit(pid); return children;
  }
  if (process.platform !== 'linux') return [];
  const children = [], seen = new Set([pid]);
  async function visit(parent) {
    let ids;
    try { ids = (await fs.readFile('/proc/' + parent + '/task/' + parent + '/children', 'utf8')).trim().split(/\s+/).filter(Boolean).map(Number); }
    catch (error) { if (error.code === 'ENOENT' || error.code === 'ESRCH') return; throw error; }
    for (const child of ids) {
      if (!Number.isSafeInteger(child) || child <= 1 || seen.has(child)) continue;
      seen.add(child);
      const identity = await processIdentity(child);
      if (!identity || identity.parent !== parent) continue;
      // A desktop browser can contain unrelated user tabs. It is never part
      // of forced console-worker cleanup, even if this panel launched it.
      const executable = await fs.realpath('/proc/' + child + '/exe').catch(() => '');
      if (/chrome|chromium|firefox|brave|vivaldi|microsoft-edge/i.test(path.basename(executable))) continue;
      await visit(child);
      children.push({pid: child, start: identity.start});
    }
  }
  await visit(pid);
  return children;
}

async function recordForcedStop(root, receipt) {
  const directory = path.join(root, 'data/state');
  await fs.mkdir(directory, {recursive: true, mode: 0o700});
  await fs.writeFile(path.join(directory, 'forced-server-stop.json'), JSON.stringify(receipt, null, 2) + '\n', {mode: 0o600});
}

async function stopPanel({pid, owner, root, inspect = inspectOwner,
  signal = (pid, name) => process.kill(pid, name), alive = processAlive,
  identity = processIdentity, children = ownedChildren, record = recordForcedStop,
  sleep = ms => new Promise(resolve => setTimeout(resolve, ms)),
  timeout = 8000, forceTimeout = 3000, force = true}) {
  const verified = await inspect(pid);
  if (!owner || !verified || !owner.start || owner.start !== verified.start ||
      path.resolve(owner.root) !== path.resolve(verified.root) || owner.version !== verified.version)
    throw Error('Panel process identity changed; automatic termination refused.');
  const descendants = await children(pid);
  const send = (target, name) => {try { signal(target, name); } catch (error) {if (error.code !== 'ESRCH') throw error;}};
  let graceful = false, legacyClosing;
  if (process.platform === 'win32') {
    const state = JSON.parse(await fs.readFile(path.join(owner.root,'data/state/window.json'),'utf8'));
    const url = new URL(state.url);
    if (state.pid !== pid || url.hostname !== '127.0.0.1' || !/^[a-f0-9]{48}$/.test(url.hash.slice(1)))
      throw Error('Panel launch identity changed; automatic termination refused.');
    const response = await fetch(url.origin+'/api/platform/shutdown', {method:'POST',
      headers:{'x-iz-key':url.hash.slice(1),origin:url.origin,'content-type':'application/json'},
      body:'{}',signal:AbortSignal.timeout(10000)});
    if (response.status === 404 || response.status === 400 && (await response.json()).error === 'Unknown action') {
      // Older verified Windows releases have no graceful shutdown endpoint.
      // The caller has already proved that the panel and its workers are idle.
      for (const child of descendants) if ((await identity(child.pid))?.start === child.start) send(child.pid,'SIGTERM');
    } else if (!response.ok) throw Error('The preceding panel refused shutdown; its session was preserved.');
    else {
      graceful = true;
      const health = await fetch(url.origin+'/health',{signal:AbortSignal.timeout(1500)})
        .then(response=>response.json()).catch(()=>null);
      if (health?.pid===pid && ['0.25','0.3.0'].includes(health.edition) && !health.handoverProtocol)
        legacyClosing = {url,token:url.hash.slice(1)};
    }
  }
  if (!graceful) send(pid, 'SIGTERM');
  for (let elapsed = 0; await alive(pid); elapsed += 100) {
    if (elapsed >= timeout) break;
    await sleep(100);
  }
  if (!await alive(pid)) return {forced: false};
  if (!force && legacyClosing) {
    // Compatibility with the old idle Windows panel, whose HTTP polling could
    // prevent server.close() from completing. Never bypass a live session.
    const response=await fetch(legacyClosing.url.origin+'/api/platform/shutdown',{
      method:'POST',headers:{'x-iz-key':legacyClosing.token,origin:legacyClosing.url.origin,
        'content-type':'application/json'},body:'{}',signal:AbortSignal.timeout(3000)});
    const status=await response.json();
    if(response.status===503&&status.error==='The panel is shutting down.'){
      await waitForIdle({root:owner.root,port:Number(legacyClosing.url.port),pid,timeout:0});
      const current=await inspect(pid);
      if(current?.start!==owner.start)throw Error('Panel process identity changed; termination refused.');
      for(const child of descendants)if((await identity(child.pid))?.start===child.start)send(child.pid,'SIGTERM');
      send(pid,'SIGTERM');
      for(let i=0;i<30&&await alive(pid);i++)await sleep(100);
      if(!await alive(pid))return {forced:false,legacyIdlePanel:true};
    }
  }
  if (!force) throw Error('The preceding panel is still stopping.');
  const current = await identity(pid);
  if (!current || current.start !== owner.start)
    throw Error('Panel process identity changed; forced termination refused.');
  const receipt = {at: new Date().toISOString(), previousRoot: owner.root, pid,
    reason: 'Graceful panel shutdown timed out', forced: true,
    consoleRestartRequired: true, nativeCleanupProven: false};
  // Preserve the restart requirement before terminating any live owner.
  await record(root, receipt);
  if (path.resolve(root) !== path.resolve(owner.root)) await record(owner.root, receipt);
  console.error('The preceding panel did not stop. Terminating its verified PC processes; restart Switch before another console session.');
  for (const child of descendants) {
    const actual = await identity(child.pid);
    if (actual?.start === child.start && !['Z', 'X'].includes(actual.state)) send(child.pid, 'SIGKILL');
  }
  // Recheck after asynchronous child cleanup so PID reuse never targets a new process.
  const actual = await identity(pid);
  if (actual?.start === owner.start) send(pid, 'SIGKILL');
  const targets = [{pid, start: owner.start}, ...descendants];
  for (let elapsed = 0;; elapsed += 100) {
    const remaining = await Promise.all(targets.map(async target => {
      const current = await identity(target.pid);
      return current?.start === target.start && !['Z', 'X'].includes(current.state) && await alive(target.pid);
    }));
    if (!remaining.some(Boolean)) break;
    if (elapsed >= forceTimeout) throw Error('The preceding panel or a console worker did not exit after forced termination.');
    await sleep(100);
  }
  return {forced: true};
}

async function findStoredOwner({root, port, inspect = inspectOwner}) {
  const candidates = new Set([path.resolve(root)]);
  for (const entry of await fs.readdir(path.dirname(root), {withFileTypes: true}))
    if (entry.isDirectory() && /^iz2s-platform(?:-run\d+)?$/.test(entry.name))
      candidates.add(path.join(path.dirname(root), entry.name));
  for (const candidate of candidates) {
    try {
      const state = JSON.parse(await fs.readFile(path.join(candidate, 'data/state/window.json'), 'utf8'));
      const url = new URL(state.url);
      if (state.preview || url.hostname !== '127.0.0.1' || Number(url.port) !== port) continue;
      const owner = await inspect(state.pid);
      if (owner && path.resolve(owner.root) === candidate && owner.version === state.version)
        return {pid: state.pid, owner};
    } catch {}
  }
  return null;
}

async function occupiedPort(port){
  let owner='';
  try{
    const {stdout}=await promisify(execFile)('ss',['-H','-ltnp','sport = :'+port],{timeout:1200});
    const line=stdout.split('\n').find(row=>/127\.0\.0\.1:|0\.0\.0\.0:|\*:/u.test(row)&&row.includes('users:'));
    const match=line?.match(/users:\(\("([^"]+)",pid=(\d+)/);
    if(match)owner=' ('+match[1]+', PID '+match[2]+')';
  }catch{/* The OS can hide the listener owner. */}
  return Error('127.0.0.1:'+port+' is already in use'+owner+'. 0.3.0 could not start.');
}

async function waitForIdle({root,port,pid,timeout=30000}){
  const origin='http://127.0.0.1:'+port;
  const state=JSON.parse(await fs.readFile(path.join(root,'data/state/window.json'),'utf8').catch(error=>{
    if(error.code==='ENOENT')return '{}';
    throw error;
  }));
  const saved=JSON.parse(await fs.readFile(path.join(root,'data/state/panel-auth.json'),'utf8').catch(error=>{
    if(error.code==='ENOENT')return '{}';
    throw error;
  }));
  const token=state.pid===pid?new URL(state.url).hash.slice(1):saved.token;
  if(!/^[a-f0-9]{48}$/.test(token||''))throw Error('Could not verify that the preceding panel is idle.');
  const read=async url=>{
    let response=await fetch(url,{headers:{'x-iz-key':token},signal:AbortSignal.timeout(3000)});
    if(response.status===404 && new URL(url).pathname==='/api/iz2s-core')
      response=await fetch(origin+require('../platform/legacy-core.json').api,{headers:{'x-iz-key':token},signal:AbortSignal.timeout(3000)});
    if(!response.ok)throw Error('Could not verify that the preceding panel is idle.');
    return response.json();
  };
  const deadline=Date.now()+timeout;
  for(;;){
    const [state,core]=await Promise.all([read(origin+'/api/state'),read(origin+'/api/iz2s-core')]);
    const link=await fetch('http://127.0.0.1:39525/status',{signal:AbortSignal.timeout(1200)})
      .then(response=>response.ok?response.json():null).catch(()=>null);
    const linkBusy=link?.service?.root===root&&(link.queued||link.fileTransfers||
      ['waiting','transferring','verifying','staged','applying'].includes(link.menu?.state)||
      Object.values(link.media||{}).some(item=>['starting','running','retrying'].includes(item?.phase)));
    const busy=state.active||core.busy||core.owned||linkBusy||
      ['queued','loading','publishing'].includes(state.hardwareOperation?.status)||
      state.jobs?.some(job=>['loading','publishing'].includes(job.status));
    if(!busy)return;
    if(Date.now()>=deadline)throw Error('The preceding panel still has an active operation. Finish it before starting 0.3.0.');
    await new Promise(resolve=>setTimeout(resolve,500));
  }
}

async function handover({ root, port, probe = async () => {
  try {
    const response = await fetch('http://127.0.0.1:' + port + '/health', { signal: AbortSignal.timeout(1500) });
    if (!response.ok) return null;
    return await response.json();
  } catch { return null; }
}, inspect = inspectOwner, find = findStoredOwner, stop = stopPanel,
restart = false, idle = waitForIdle, ...options }) {
  const health = await probe();
  const stored = health ? null : await find({root, port, inspect});
  if (!health && !stored) return false;
  const pid = health?.pid ?? stored.pid;
  const owner = stored?.owner ?? await inspect(pid);
  if (!owner || (health && (owner.version !== health.version || health.preview)))
    throw Error('The panel port '+port+' is occupied by an unverified process'+
      (Number.isSafeInteger(pid)?' (PID '+pid+')':'')+'. Close it before starting 0.3.0.');
  if(path.resolve(owner.root)!==path.resolve(root)&&!['RUN24','0.3.0','0.3.0'].includes(owner.edition))
    throw Error('Port '+port+' belongs to '+(owner.edition||'another panel')+' (PID '+pid+'). Close it before starting 0.3.0.');
  if (path.resolve(owner.root) === path.resolve(root) && !restart) return false;
  if(path.resolve(owner.root)!==path.resolve(root))await idle({root:owner.root,port,pid,timeout:options.timeout});
  console.log('Stopping the preceding IZ2S Platform panel before starting this release.');
  await stop({pid, owner, root, inspect, ...options});
  return true;
}

module.exports = { handover, inspectOwner, stopPanel, processIdentity, processAlive, ownedChildren, findStoredOwner, occupiedPort, waitForIdle };
