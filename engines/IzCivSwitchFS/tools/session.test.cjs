'use strict';
const test=require('node:test'),assert=require('node:assert/strict');
const fs=require('node:fs/promises'),os=require('node:os'),path=require('node:path');
const {EventEmitter}=require('node:events'),{PassThrough}=require('node:stream');
const {create}=require('../session.cjs');
async function fixture(t,{output='',preview=false,hold=false,transportError='',platform='linux',windowsWifi}={}){
 const root=await fs.mkdtemp(path.join(os.tmpdir(),'iz2sciv-session-'));
 t.after(()=>fs.rm(root,{recursive:true,force:true}));
 await fs.mkdir(path.join(root,'data/state'),{recursive:true});
 await fs.writeFile(path.join(root,'data/state/iz2sciv.json'),JSON.stringify({sdReady:true}));
 const calls=[],children=[];
 let stopped=false;
 const transportState={setup:{supported:true,dependenciesReady:true,capabilitiesReady:true,radios:['phy0']},prepared:true,loaderReady:true,suggestedHost:'192.168.1.9',error:transportError,phase:'ready',index:19,total:19};
 const ldn={
  action:async body=>{calls.push(body);if(body.action==='discard')transportState.loaderReady=false;if(body.host)transportState.suggestedHost=body.host;},waitForOperation:async()=>{},refresh:async()=>{},
  snapshot:async()=>({civswitchLdn:transportState,log:[],checks:{items:[]},connections:{},history:[]}),
  stop:async()=>{stopped=true;},configureConnection:async()=>({ok:true}),
 };
 const launch=(program,args)=>{
  calls.push({program,args});
  const child=new EventEmitter();child.stdout=new PassThrough();child.stderr=new PassThrough();
  child.kill=()=>{setImmediate(()=>child.emit('close',null));return true;};children.push(child);
  if(!hold)setImmediate(()=>{child.stdout.write(output);child.emit('close',0);});
  return child;
 };
 const session=create({root,preview,ldn,launch,platform,windowsWifi,findFtp:async()=> '192.168.1.9',addressOptions:async()=>[]});
 return {session,calls,children,ldn,get stopped(){return stopped;},transportState,root};
}
test('preview refuses every side effect, including key import',async t=>{
 const {session,calls}=await fixture(t,{preview:true});
 for(const action of ['confirm-sd','prepare','permissions','import-keys','start','install','launch','check-launcher','select-host','find-addresses'])
  await assert.rejects(session.action({action,confirm:true,replaceTrigger:true,freshBoot:true}),/Preview/);
 assert.equal(calls.length,0);
});
test('save installation discovers automatically and invokes only the IZ2S installer',async t=>{
 const {session,calls}=await fixture(t,{output:'Installed. Restart the game, then open Load Game menu.'});
 await assert.rejects(session.action({action:'install'}),/IZ2SCIV/);
 await session.action({action:'install',replaceTrigger:true});await session.waitForOperation();
 assert.deepEqual(calls[0],{action:'check-loader',automatic:true});
 assert.match(calls[1].args[0],/IzCivSwitchFS[\\/]tools[\\/]persist-install\.mjs$/);
 assert.deepEqual(calls[1].args.slice(1),['--chain','192.168.1.9']);
 assert.equal((await session.snapshot()).iz2sciv.installed,true);
 assert(!calls.some(x=>x.action==='install'));
});
test('zero exit without completion marker never proves native startup or installation',async t=>{
 const {session}=await fixture(t,{output:'PAYLOAD ERROR: unavailable'});
 for(const action of ['install','launch']){
  await session.action({action,replaceTrigger:true,freshBoot:true});await session.waitForOperation();
  const state=(await session.snapshot()).iz2sciv;
  assert.equal(state.phase,'failed');assert.equal(state.installed,false);assert.match(state.error,/not confirmed/);
 }
});
test('launch uses default IZ2S chain, requires fresh boot and confirms native marker',async t=>{
 const {session,calls}=await fixture(t,{output:'=== IZ2S CORE IS RUNNING ==='});
 await assert.rejects(session.action({action:'launch'}),/fresh console boot/);
 await session.action({action:'launch',freshBoot:true});await session.waitForOperation();
 assert.deepEqual(calls[0],{action:'check-loader',automatic:true});
 assert.match(calls[1].args[0],/tools[\\/]send\.mjs$/);
 assert.deepEqual(calls[1].args.slice(1),['192.168.1.9']);
 assert.equal((await session.snapshot()).iz2sciv.phase,'launch-done');
});
test('LDN delivery cannot invoke its legacy save installer',async t=>{
 const {session,calls}=await fixture(t);
 await session.action({action:'start',confirm:true});await session.waitForOperation();
 assert.deepEqual(calls,[{action:'start',confirm:true,freshLobby:true,automatic:true,installAfterSuccess:false}]);
 assert.equal((await session.snapshot()).iz2sciv.phase,'start-done');
});
test('transport errors prevent sending the native chain',async t=>{
 const {session,calls}=await fixture(t,{transportError:'No game loader found'});
 await session.action({action:'launch',freshBoot:true});await session.waitForOperation();
 assert.equal(calls.length,1);assert.equal((await session.snapshot()).iz2sciv.phase,'failed');
});
test('manual SD confirmation needs explicit copying and never calls FTP',async t=>{
 const {session,calls}=await fixture(t);
 await assert.rejects(session.action({action:'confirm-sd'}),/extracted/);
 await session.action({action:'confirm-sd',copied:true});
 const state=(await session.snapshot()).iz2sciv;
 assert.equal(state.sdReady,true);assert.equal(state.sdPreparation,'manual-zip');
 assert.equal(state.phase,'confirm-sd-done');assert.equal(calls.length,0);
 await assert.rejects(session.action({action:'prepare-sd'}),/Unknown/);
});
test('post-launch FTP verification remains read-only and is not the SD preparation step',async t=>{
 const {session,calls}=await fixture(t,{output:'IZ2SCIV_SD_VERIFIED'});
 await session.action({action:'check-launcher'});await session.waitForOperation();
 assert.equal(calls[0].args.includes('--verify-only'),true);
 assert.equal((await session.snapshot()).connected,false);
});
test('busy operations reject reentry and connection changes; Stop waits for exit',async t=>{
 const f=await fixture(t,{hold:true});
 await f.session.action({action:'check-launcher'});
 while(!f.children.length)await new Promise(setImmediate);
 assert.equal((await f.session.snapshot()).iz2sciv.busy,true);
 await assert.rejects(f.session.action({action:'prepare'}),/Wait/);
 await assert.rejects(f.session.configureConnection({}),/Stop/);
 await f.session.stop();assert.equal(f.stopped,true);
 const state=(await f.session.snapshot()).iz2sciv;
 assert.equal(state.busy,false);assert.equal(state.phase,'interrupted');
});
test('RUN25 exposes exactly SB4 and IZ2SCiv, and rejects old Civ engines',()=>{
 const root=path.resolve(__dirname,'../../..');
 const loaders=require('../../../engines/index.cjs').create(root).list();
 const catalog=require('../../../core/packages/store-catalog.cjs');
 assert.deepEqual(catalog.normalizeSnapshot({platform:{loaders}}).platform.loaders.map(x=>x.id).sort(),['iz2s-core','iz2sciv-core']);
 for(const id of ['civswitch','civswitch-ldn'])assert.equal(catalog.engineAvailable(loaders.find(x=>x.id===id),'original'),false);
});

test('selected IP is protocol-checked and rechecked before native transmission',async t=>{
 const {session,calls}=await fixture(t,{output:'=== IZ2S CORE IS RUNNING ==='});
 await assert.rejects(session.action({action:'select-host',host:'8.8.8.8'}),/private/);
 assert.equal(calls.length,0);
 await session.action({action:'select-host',host:'192.168.1.33'});await session.waitForOperation();
 assert.equal((await session.snapshot()).iz2sciv.selectedHost,'192.168.1.33');
 await session.action({action:'launch',freshBoot:true});await session.waitForOperation();
 assert.deepEqual(calls.slice(0,2),[{action:'check-loader',automatic:false,host:'192.168.1.33'},{action:'check-loader',automatic:false,host:'192.168.1.33'}]);
 assert.equal(calls[2].args[1],'192.168.1.33');
});
test('failed address verification cannot select or send to the candidate',async t=>{
 const {session,calls,transportState}=await fixture(t);
 transportState.error='Loader did not respond';
 await session.action({action:'select-host',host:'192.168.1.33'});await session.waitForOperation();
 const state=(await session.snapshot()).iz2sciv;assert.equal(state.selectedHost,'');assert.equal(state.phase,'failed');
 assert.equal(calls.length,1);assert(!calls.some(x=>x.program));
});
test('automatic mode clears the explicit target and resumes discovery',async t=>{
 const {session,calls}=await fixture(t);
 await session.action({action:'select-host',host:'192.168.1.33'});await session.waitForOperation();
 await session.action({action:'auto-host'});
 await session.action({action:'check-loader'});await session.waitForOperation();
 assert.deepEqual(calls[1],{action:'check-loader',automatic:true});
 assert.equal((await session.snapshot()).iz2sciv.selectedHost,'');
});
test('multiple loader results are preserved for selection, with no payload sent',async t=>{
 const {root}=await fixture(t);
 const session=create({root,settings:{},connections:{snapshot:()=>({devices:{}})},
  inspect:async()=>({supported:false,radios:[],dependenciesReady:false}),
  discoverNetwork:async()=>['192.168.1.9','192.168.1.10'],
  protocolProbe:async()=>{throw Error('Ambiguous results must not be used');},
  addressOptions:async({found})=>found.map(address=>({address,verified:true})),
  launch:()=>{throw Error('No native process expected');}});
 await session.action({action:'find-addresses'});await session.waitForOperation();
 const state=(await session.snapshot()).iz2sciv;
 assert.equal(state.phase,'failed');assert.match(state.error,/Several/);
 assert.deepEqual(state.addressOptions.map(x=>x.address),['192.168.1.9','192.168.1.10']);
});
test('no loader response still exposes unverified network candidates',async t=>{
 const {root}=await fixture(t);
 const session=create({root,settings:{},connections:{snapshot:()=>({devices:{}})},
  inspect:async()=>({supported:false,radios:[],dependenciesReady:false}),
  discoverNetwork:async()=>[],addressOptions:async()=>[{address:'192.168.1.22',verified:false}],
  launch:()=>{throw Error('No process expected');}});
 await session.action({action:'find-addresses'});await session.waitForOperation();
 const state=(await session.snapshot()).iz2sciv;
 assert.equal(state.phase,'failed');assert.deepEqual(state.addressOptions,[{address:'192.168.1.22',verified:false}]);
});
test('candidate list filters public/off-network IPs and distinguishes loader replies',async()=>{
 const list=require('./address-options.cjs').list;
 const rows=await list({found:['192.168.1.9'],known:['192.168.1.8','8.8.8.8','10.1.1.1'],
  readNeighbours:async()=>['192.168.1.20','192.168.1.9'],
  attached:async({known})=>({preferred:known.filter(x=>x.startsWith('192.168.1.'))})});
 assert.deepEqual(rows,[{address:'192.168.1.9',verified:true},{address:'192.168.1.8',verified:false},{address:'192.168.1.20',verified:false}]);
});
test('explicit IP survives lobby delivery and is passed for the subsequent loader check',async t=>{
 const {session,calls}=await fixture(t);
 await session.action({action:'select-host',host:'192.168.1.33'});await session.waitForOperation();
 await session.action({action:'start',confirm:true});await session.waitForOperation();
 assert.deepEqual(calls[1],{action:'start',confirm:true,freshLobby:true,automatic:false,host:'192.168.1.33',installAfterSuccess:false});
 assert.equal((await session.snapshot()).iz2sciv.selectedHost,'192.168.1.33');
});
test('old installed save requires an update after reloading persisted state',async t=>{
 const f=await fixture(t);
 await f.session.snapshot(); // Finish the first instance's asynchronous state read before replacing the fixture.
 await fs.writeFile(path.join(f.root,'data/state/iz2sciv.json'),JSON.stringify({installed:true,sdReady:true,phase:'install-done'}));
 // A separate instance reads the changed record; no console actions are needed.
 const session=create({root:f.root,ldn:{snapshot:async()=>({civswitchLdn:f.transportState,log:[],connections:{}})}});
 const s=(await session.snapshot()).iz2sciv;
 assert.equal(s.installed,false);assert.equal(s.needsSaveUpdate,true);assert.equal(s.saveName,'IZ2SCIV.Civ7Save');
});
test('manual Wi-Fi recovery calls only the bounded network recovery script',async t=>{
 const {session,calls}=await fixture(t,{output:'CIV7_LDN_NETWORK_RESTORED'});
 await session.action({action:'restore-network'});await session.waitForOperation();
 assert.equal(calls.length,1);assert.equal(calls[0].program,'bash');assert.equal(calls[0].args[1],'--restore');assert.match(calls[0].args[0],/network-panel.sh$/);
 assert.equal((await session.snapshot()).iz2sciv.phase,'restore-network-done');
});
test('the last IZ2SCiv attempt and its output survive a server restart',async t=>{
 const f=await fixture(t,{output:'Installed. Restart the game, then open Load Game menu.'});
 await f.session.action({action:'install',replaceTrigger:true});await f.session.waitForOperation();
 const session=create({root:f.root,ldn:{snapshot:async()=>({civswitchLdn:f.transportState,log:[],connections:{}})}});
 const s=await session.snapshot();assert.equal(s.lastAttempt.status,'complete');assert.equal(s.iz2sciv.installed,true);assert(s.log.some(e=>e.message.includes('Installed. Restart')));
});

test('downloadable attempts include the LDN details of their own transfer',async t=>{
 const f=await fixture(t),rows=[];
 f.ldn.action=async body=>{f.calls.push(body);rows.push({at:new Date().toISOString(),type:'civswitch-ldn',message:'[net] fixture network restored'});};
 f.ldn.snapshot=async()=>({civswitchLdn:f.transportState,log:rows,connections:{}});
 await f.session.action({action:'start',confirm:true});await f.session.waitForOperation();
 const state=await f.session.snapshot(),ledger=require('../../../core/session/ledger.cjs').createLedger(f.root);
 const output=await ledger.recent(state.lastAttempt.id);assert(output.some(e=>e.message==='[net] fixture network restored'));assert(output.some(e=>e.message==='IZ2SCiv: start-done'));
});

test('failed installation retains its operation after a server restart for correct UI recovery',async t=>{
 const f=await fixture(t,{output:'Unconfirmed result'});
 await f.session.action({action:'install',replaceTrigger:true});await f.session.waitForOperation();
 const session=create({root:f.root,ldn:{snapshot:async()=>({civswitchLdn:f.transportState,log:[],connections:{}})}});
 const state=(await session.snapshot()).iz2sciv;
 assert.equal(state.phase,'failed');assert.equal(state.lastAction,'install');assert.equal(state.installed,false);
});

test('reset connection forgets stale console state and preserves PC and SD preparation',async t=>{
 const {session,calls,transportState,root}=await fixture(t);
 await session.action({action:'start',confirm:true});await session.waitForOperation();
 calls.length=0;
 await session.action({action:'reset-connection'});
 const state=(await session.snapshot()).iz2sciv;
 assert.deepEqual(calls,[{action:'discard'}]);
 assert.equal(state.loaderReady,false);assert.equal(state.phase,'connection-reset');
 assert.equal(state.installed,false);assert.equal(state.sdReady,true);assert.equal(state.prepared,true);
 assert.equal(transportState.loaderReady,false);
 assert.equal(JSON.parse(await fs.readFile(path.join(root,'data/state/iz2sciv.json'))).phase,'connection-reset');
 await assert.rejects(session.action({action:'start'}),/fresh.*lobby/);
});


test('native Windows detects Wi-Fi once per refresh and installs only through a verified open loader',async t=>{
 let inspections=0;
 const windowsWifi={inspect:async()=>{inspections++;return {adapters:[{id:'wifi-id',name:'Wi-Fi'}],ldnSupported:false};}};
 const {session,calls}=await fixture(t,{platform:'win32',windowsWifi,output:'Installed. Restart the game, then open Load Game menu.'});
 await session.snapshot();await session.snapshot();assert.equal(inspections,1);
 await session.action({action:'wifi-status'});assert.equal(inspections,2);
 for(const action of ['prepare','permissions','start'])await assert.rejects(session.action({action,confirm:true}),/Native Windows LDN/);
 assert.equal(calls.length,0);
 await session.action({action:'install',replaceTrigger:true});await session.waitForOperation();
 assert.equal(calls[0].action,'check-loader');assert.equal((await session.snapshot()).iz2sciv.installed,true);
});

test('concurrent Windows snapshots share one check and a retry replaces failed evidence',async t=>{
 let inspections=0,resolve;
 const windowsWifi={inspect:()=>{inspections++;return new Promise(r=>{resolve=r;});}};
 const {session}=await fixture(t,{platform:'win32',windowsWifi});
 const first=session.snapshot(),second=session.snapshot();
 while(!resolve)await new Promise(setImmediate);
 resolve({adapters:[],status:'no-adapter',ldnSupported:false});
 await Promise.all([first,second]);assert.equal(inspections,1);
 windowsWifi.inspect=async()=>{throw Error('Timeout');};
 await session.action({action:'wifi-status'});
 const failed=(await session.snapshot()).iz2sciv.windowsWifi;
 assert.equal(failed.status,'inspection-failed');assert.equal(failed.error,'Timeout');
 windowsWifi.inspect=async()=>({adapters:[{id:'new-adapter'}],status:'ldn-unavailable',ldnSupported:false});
 await session.action({action:'wifi-status'});
 const retried=(await session.snapshot()).iz2sciv.windowsWifi;
 assert.equal(retried.adapters[0].id,'new-adapter');assert.equal(retried.error,undefined);
});
