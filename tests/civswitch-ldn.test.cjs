'use strict';
const {test} = require('node:test'), assert = require('node:assert/strict');
const fs = require('node:fs/promises'), path = require('node:path'), os = require('node:os');
const {EventEmitter} = require('node:events'), {PassThrough} = require('node:stream');
const integration = require('../engines/CivSwitchLDN/integration.cjs');
const {create} = require('../engines/CivSwitchLDN/session.cjs');
const root = path.resolve(__dirname, '..');
// Synthetic test entries only; no console keys are read by these tests.
const keys = ['aes_kek_generation_source', 'aes_key_generation_source', 'master_key_00', 'master_key_12']
  .map(name => name + ' = ' + '00'.repeat(16)).join('\n');
async function fixture(t, overrides = {}) {
  const dir = await fs.mkdtemp(path.join(os.tmpdir(), 'civ7-ldn-'));
  t.after(() => fs.rm(dir, {recursive: true, force: true}));
  const calls = [], processes = [];
  const setup = {supported: true, radios: ['phy0'], keysReady: true, dependenciesReady: true, capabilitiesReady: true, networkManager: true};
  let clock = 0;
  const options = {root: dir, settings: {}, configuration: {},
    // No ARM/ADV object is supplied: the LAN mode must be independent of HID.
    connections: {snapshot: () => ({devices: {server: {address: '10.42.0.1'}, lan: {address: '10.42.0.20'}}}), update: async () => {}},
    inspect: async () => ({...setup}), discoverLoader:async()=>['10.42.0.20'], now: () => clock, pause: async ms => {clock += ms;}, loaderWaitMs: 2000,
    launch(program, args) {
      calls.push({program, args});
      const child = new EventEmitter(); child.stdout = new PassThrough(); child.stderr = new PassThrough();
      child.kill = () => queueMicrotask(() => child.emit('close', null, 'SIGTERM'));
      processes.push(child);
      queueMicrotask(() => {
        const runner = args.some(arg => arg.endsWith('panel-runner.cjs'));
        child.stdout.write(runner ? 'CIV7_LDN_PAYLOAD_LINES=19\nSending payload: 100% (19/19 messages)\nCIV7_LDN_NETWORK_RESTORED\nCIV7_LDN_DELIVERY_COMPLETE\n' :
          args.some(arg => arg.endsWith('probe.py')) ? 'CIVSWITCH_LOADER_VERIFIED\n' : 'INSTALL DONE\n');
        child.emit('close', 0, null);
      });
      return child;
    }, ...overrides};
  const manager = create(options);
  async function wait() {
    for (let i = 0; i < 300; i++) {const state = await manager.snapshot(); if (!state.civswitchLdn.busy) return state; await new Promise(resolve => setTimeout(resolve, 2));}
    throw Error('LDN fixture did not finish');
  }
  return {dir, manager, wait, calls, setup, options, processes};
}
test('CIV7 LAN is a separate Core only engine on Linux x64 and ARM64', () => {
  const engines = require('../engines/index.cjs').create(root);
  assert(engines.available('civswitch-ldn', 'original'));
  assert(!engines.available('civswitch-ldn', 'izlauncher'));
  const engine = engines.list().find(item => item.id === 'civswitch-ldn');
  assert.deepEqual(engine.platforms, ['linux-x64', 'linux-arm64', 'win32-x64', 'win32-arm64']);
  assert.equal(engine.consolePort, 9026);
});
test('Load Game connects over LAN without a Wi-Fi radio, keys, or LDN permissions',async t=>{
 const f=await fixture(t);f.setup.radios=[];f.setup.keysReady=false;f.setup.capabilitiesReady=false;f.setup.networkManager=false;
 await f.manager.action({action:'check-loader',host:'10.42.0.20',networkMode:'lan'});
 const state=await f.wait();
 assert.equal(state.civswitchLdn.loaderReady,true);
 assert.equal(state.civswitchLdn.networkMode,'lan');
 assert.equal(state.checks.items.find(row=>row.id==='server').ready,true);
 assert.equal(f.calls.length,1);
 assert(f.calls[0].args.some(arg=>arg.endsWith('probe.py')));
 assert(f.calls[0].args.includes('10.42.0.20'));
 assert(!f.calls.some(call=>call.args.some(arg=>arg.endsWith('panel-runner.cjs'))));
 await assert.rejects(f.manager.action({action:'configure',networkMode:'invalid'}),/LAN or Wi-Fi/);
});
test('prod.keys is required, validated, stored privately, and absent from state and logs', async t => {
  const f = await fixture(t); f.setup.keysReady = false;
  await assert.rejects(f.manager.action({action: 'start', confirm: true}), /prod.keys/);
  assert.equal(f.calls.length, 0);
  await assert.rejects(f.manager.action({action: 'import-keys', content: 'bad'}), /missing valid LDN entries/);
  await f.manager.action({action: 'import-keys', content: keys});
  const file = integration.paths(f.dir).keys;
  assert.equal(await fs.readFile(file, 'utf8'), keys);
  assert.equal((await fs.stat(file)).mode & 0o777, 0o600);
  assert.equal((await fs.stat(path.dirname(file))).mode & 0o777, 0o700);
  const snapshot = JSON.stringify(await f.manager.snapshot());
  assert(!snapshot.includes('00'.repeat(16)));
  assert(!snapshot.includes('aes_kek_generation_source'));
  assert.throws(() => integration.validateKeys('x'.repeat(65537)), /64 KB/);
});
test('one LDN delivery is followed by loader verification and optional consented save installation', async t => {
  const f = await fixture(t);
  await f.manager.action({action: 'start', confirm: true, installAfterSuccess: true, deleteSingleSaves: true});
  const state = await f.wait();
  assert.equal(state.civswitchLdn.phase, 'installed');
  assert.equal(state.civswitchLdn.index, 19);
  assert.equal(f.calls.length, 3);
  assert(f.calls[0].args.some(arg => arg.endsWith('panel-runner.cjs')));
  assert(f.calls[1].args.some(arg => arg.endsWith('probe.py')));
  assert(f.calls[2].args.some(arg => arg.endsWith('installer/install.py')));
  assert.equal(f.calls[1].args.at(-1), '9026');
  assert(!state.checks.items.some(row => ['arm', 'usb'].includes(row.id)));
});
test('save deletion, game readiness and helper permissions are enforced before any network changes', async t => {
  const f = await fixture(t);
  await assert.rejects(f.manager.action({action: 'start'}), /fresh/);
  await assert.rejects(f.manager.action({action: 'start', confirm: true, installAfterSuccess: true}), /ALL single-player/);
  f.setup.capabilitiesReady = false;
  await assert.rejects(f.manager.action({action: 'start', confirm: true}), /network permissions/);
  assert.equal(f.calls.length, 0);
});
test('unconfirmed network restoration prevents loader probing and save installation', async t => {
  const f = await fixture(t);
  f.options.launch = (program, args) => {
    f.calls.push({program, args});
    const child = new EventEmitter(); child.stdout = new PassThrough(); child.stderr = new PassThrough();
    queueMicrotask(() => {child.stdout.write('CIV7_LDN_DELIVERY_COMPLETE\n'); child.emit('close', 0);});
    return child;
  };
  const manager = create(f.options);
  await manager.action({action: 'start', confirm: true, installAfterSuccess: true, deleteSingleSaves: true});
  while ((await manager.snapshot()).civswitchLdn.busy) await new Promise(resolve => setTimeout(resolve, 2));
  assert.equal(f.calls.length, 1);
  assert.match((await manager.snapshot()).civswitchLdn.error, /restoration was not confirmed/);
});
test('STOP waits for the child restoration to finish and never resends the payload', async t => {
  let handle, started;
  const running = new Promise(resolve => {started = resolve;});
  const f = await fixture(t, {launch(program, args) {
    const child = new EventEmitter(); child.stdout = new PassThrough(); child.stderr = new PassThrough();
    child.kill = () => {child.signalled = true;}; handle = child; started(); return child;
  }});
  await f.manager.action({action: 'start', confirm: true}); await running;
  let stopped = false; const stop = f.manager.stop().then(() => {stopped = true;});
  await new Promise(resolve => setTimeout(resolve, 10));
  assert.equal(stopped, false); assert.equal(handle.signalled, true);
  assert.equal((await f.manager.snapshot()).active.status, 'stopping');
  handle.stdout.write('CIV7_LDN_NETWORK_RESTORED\n'); handle.emit('close', 143);
  await stop;
  const state = await f.manager.snapshot();
  assert.equal(state.active, null); assert.equal(state.civswitchLdn.phase, 'interrupted');
  const restored = create(f.options);
  assert.equal((await restored.snapshot()).active, null);
  await assert.rejects(restored.action({action: 'start', confirm: true}), /fresh lobby/);
});
test('loader refusal retries probes without repeating LDN delivery or claiming a connection', async t => {
  const f = await fixture(t), launch = f.options.launch;
  f.options.launch = (program, args) => {
    if (!args.some(arg => arg.endsWith('probe.py'))) return launch(program, args);
    f.calls.push({program, args});
    const child = new EventEmitter(); child.stdout = new PassThrough(); child.stderr = new PassThrough();
    queueMicrotask(() => {child.stderr.write('CIVSWITCH_LOADER_NOT_RUNNING'); child.emit('close', 1);});
    return child;
  };
  const manager = create(f.options); await manager.action({action: 'start', confirm: true});
  while ((await manager.snapshot()).civswitchLdn.busy) await new Promise(resolve => setTimeout(resolve, 2));
  const state = await manager.snapshot();
  assert.equal(state.connected, false); assert.equal(state.civswitchLdn.phase, 'entered');
  assert.equal(f.calls.filter(call => call.args.some(arg => arg.endsWith('panel-runner.cjs'))).length, 1);
  assert(f.calls.filter(call => call.args.some(arg => arg.endsWith('probe.py'))).length >= 2);
  assert(!state.civswitchLdn.error.includes('Traceback'));
});
test('preview and invalid radio, IP, MAC, and timing settings cannot send commands', async t => {
  const preview = await fixture(t, {preview: true});
  await assert.rejects(preview.manager.action({action: 'start', confirm: true}), /Preview/);
  const f = await fixture(t);
  for (const change of [{host: '1.1.1.1'}, {phy: 'phy0;id'}, {hostMac: 'bad'}, {chatDelay: -1}, {handshakeScale: 100}])
    await assert.rejects(f.manager.action({action: 'configure', ...change}));
  assert.equal(f.calls.length, 0);
});
test('ordinary progress polling reuses setup readiness; Continue explicitly rechecks permissions', async t => {
  let clock=0,inspections=0;
  const f=await fixture(t,{now:()=>clock,inspect:async()=>{inspections++;return {supported:true,radios:['phy0'],keysReady:true,dependenciesReady:true,capabilitiesReady:true,networkManager:true};}});
  await f.manager.snapshot();
  for(let i=0;i<30;i++){clock+=2000;await f.manager.refresh();await f.manager.snapshot();}
  assert.equal(inspections,1);
  const result=await f.manager.action({action:'refresh'});
  assert.equal(result.prepared,true);assert.equal(inspections,2);
});
test('panel adaptation preserves the upstream CLI and limits network suspension to one radio', async () => {
  const folder = path.join(root, 'engines/CivSwitchLDN');
  const network = await fs.readFile(path.join(folder, 'bash/network-panel.sh'), 'utf8');
  assert(!network.includes('nmcli networking off'));
  assert(network.includes('managed no') && network.includes('managed yes'));
  assert(network.includes('CIV7_LDN_NETWORK_RESTORED'));
  const cli = await fs.readFile(path.join(folder, 'run.js'), 'utf8');
  assert(cli.includes('Run the persistent installer?'));
  const installer = await fs.readFile(path.join(folder, 'installer/install.py'), 'utf8');
  assert(installer.includes('save:/saves/Single/IZ2S.Civ7Save'));
  assert(!installer.includes('XSTRIG.Civ7Save'));
});

test('LDN exposes only PC and console tiles and discards old errors without delivery',async t=>{
 const f=await fixture(t);await f.manager.snapshot();await fs.mkdir(path.join(f.dir,'data/state'),{recursive:true});
 await fs.writeFile(path.join(f.dir,'data/state/civswitchldn.json'),JSON.stringify({phase:'interrupted',index:8,error:'old timeout',phy:'phy0',host:'10.42.0.20'}));
 const manager=create(f.options);const before=await manager.snapshot();assert.deepEqual(before.checks.items.map(row=>row.id),['server','lan']);assert.equal(before.civswitchLdn.restoredAttempt,true);
 await manager.action({action:'discard'});const s=(await manager.snapshot()).civswitchLdn;
 assert.equal(s.index,0);assert.equal(s.phase,'idle');assert.equal(s.error,undefined);assert.equal(s.restoredAttempt,false);assert.equal(f.calls.length,0);
});

test('LDN delivery needs no console IP and verifies the loader discovered after Wi-Fi restoration',async t=>{
 const found=[];const f=await fixture(t,{discoverLoader:async phy=>{found.push(phy);return ['192.168.1.77'];}});
 const before=await f.manager.snapshot();assert.equal(before.civswitchLdn.suggestedHost,'');
 await f.manager.action({action:'configure',host:''});
 await f.manager.action({action:'start',confirm:true,host:''});const state=await f.wait();
 assert.deepEqual(found,['phy0']);assert.equal(state.connected,true);assert.equal(state.civswitchLdn.host,'192.168.1.77');
 assert.equal(state.civswitchLdn.networkRestored,true);
 assert(f.calls.find(call=>call.args.some(arg=>arg.endsWith('probe.py'))).args.includes('192.168.1.77'));
 assert.equal(f.calls.filter(call=>call.args.some(arg=>arg.endsWith('panel-runner.cjs'))).length,1);
});
test('checking an existing loader can discover its address without sending another LDN payload',async t=>{
 const f=await fixture(t);await f.manager.action({action:'check-loader',host:''});const state=await f.wait();
 assert.equal(state.connected,true);assert(!f.calls.some(call=>call.args.some(arg=>arg.endsWith('panel-runner.cjs'))));
});

test('Windows can pair with a saved loader without Linux dependencies or touching a Wi-Fi adapter',async t=>{
 const probes=[];const f=await fixture(t,{platform:'win32',protocolProbe:async host=>{probes.push(host);return true;}});
 f.setup.supported=false;f.setup.dependenciesReady=false;f.setup.keysReady=false;f.setup.radios=[];
 await f.manager.action({action:'check-loader',host:'10.42.0.20',networkMode:'lan'});
 assert.equal((await f.wait()).civswitchLdn.loaderReady,true);
 assert.deepEqual(probes,['10.42.0.20']);assert.equal(f.calls.length,0);
 await assert.rejects(f.manager.action({action:'start',confirm:true}),/requires native Linux/);
});
test('automatic LAN pairing replaces a stale Wi-Fi IP without sending LDN or HID input',async t=>{
 const searches=[];const f=await fixture(t,{discoverNetwork:async options=>{searches.push(options);return ['10.42.0.20'];}});
 await f.manager.action({action:'configure',host:'192.168.1.122'});
 await f.manager.action({action:'check-loader',host:'192.168.1.122',networkMode:'lan',automatic:true});
 const s=await f.wait();assert.equal(s.civswitchLdn.suggestedHost,'10.42.0.20');assert.equal(s.civswitchLdn.loaderReady,true);
 assert.equal(searches[0].mode,'lan');assert(searches[0].known.includes('192.168.1.122'));
 assert.equal(f.calls.length,1);assert(f.calls[0].args.some(a=>a.endsWith('probe.py')));
});
test('ambiguous discovery never selects a console or installs a save',async t=>{
 const f=await fixture(t,{discoverNetwork:async()=>['10.42.0.20','10.42.0.21']});
 await f.manager.action({action:'check-loader',networkMode:'lan',automatic:true});
 const s=await f.wait();assert.equal(s.civswitchLdn.loaderReady,false);assert.match(s.civswitchLdn.error,/Several/);assert.equal(f.calls.length,0);
});

for(const restored of [false,true])test('a failed LDN association selects the recovery step from observed Wi-Fi restoration: '+restored,async t=>{
 const f=await fixture(t);
 f.options.launch=(program,args)=>{
  f.calls.push({program,args});const child=new EventEmitter();child.stdout=new PassThrough();child.stderr=new PassThrough();
  queueMicrotask(()=>{child.stdout.write('CIV7_LDN_PAYLOAD_LINES=19\n');child.stderr.write('Error: ConnectionError: LDN association failed\n');if(restored)child.stdout.write('CIV7_LDN_NETWORK_RESTORED\n');child.emit('close',1);});return child;
 };
 const manager=create(f.options);await manager.action({action:'start',confirm:true});
 while((await manager.snapshot()).civswitchLdn.busy)await new Promise(r=>setTimeout(r,2));
 const state=await manager.snapshot();assert.equal(state.civswitchLdn.phase,'interrupted');assert.equal(state.civswitchLdn.retryStep,restored?3:1);assert.equal(state.connected,false);assert.equal(f.calls.length,1,'delivery must never repeat automatically');
});


test('a renumbered sole Wi-Fi radio replaces the stale selection and persists without sending input', async t => {
  const f = await fixture(t);
  await f.manager.snapshot();
  f.setup.radios = ['phy7'];
  await f.manager.refresh(true);
  const state = await f.manager.snapshot();
  assert.equal(state.civswitchLdn.phy, 'phy7');
  assert.equal(state.civswitchLdn.prepared, true);
  assert.equal(state.checks.items.find(row => row.id === 'server').ready, true);
  assert.equal(JSON.parse(await fs.readFile(path.join(f.dir, 'data/state/civswitchldn.json'))).phy, 'phy7');
  assert.equal(f.calls.length, 0);
});
test('radio recovery preserves valid choices and requires an explicit choice for ambiguous adapters', async t => {
  const f = await fixture(t); await f.manager.snapshot();
  f.setup.radios = ['phy0', 'phy7']; await f.manager.refresh(true);
  assert.equal((await f.manager.snapshot()).civswitchLdn.phy, 'phy0');
  f.setup.radios = ['phy6', 'phy7']; await f.manager.refresh(true);
  const missing = await f.manager.snapshot();
  assert.equal(missing.civswitchLdn.prepared, false);
  assert.match(missing.checks.items[0].detail, /Select an available Wi-Fi radio/);
  f.setup.radios = []; await f.manager.refresh(true);
  assert.match((await f.manager.snapshot()).checks.items[0].detail, /No Wi-Fi radio found/);
  assert.equal(f.calls.length, 0);
});
