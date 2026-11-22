'use strict';
const test=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),vm=require('node:vm');
const root=path.resolve(__dirname,'../../..');
const html=fs.readFileSync(path.join(root,'panel/ui/panel.html'),'utf8');
function fixture({keysReady=false,importError='',preview=false,link=null,ftp=null,isUnavailable=()=>false,isPanelBusy=()=>false,resetSession,initial={}}={}){
 const elements=new Map();
 const element=()=>({hidden:false,disabled:false,checked:false,value:'',files:[],textContent:'',dataset:{},children:[],attributes:{},
  classList:{toggle(){}},parentElement:{classList:{toggle(){}}},
  setAttribute(k,v){this.attributes[k]=v;},removeAttribute(k){delete this.attributes[k];},replaceChildren(){this.children=[];},append(x){this.children.push(x);},
  querySelector(){return {focus(){}};},focus(){this.focused=true;},click(){return this.onclick?.();}});
 for(const match of html.matchAll(/\bid="([^"]+)"/g))elements.set(match[1],element());
 const document={getElementById(id){assert(elements.has(id),'Missing UI element '+id);return elements.get(id);},createElement:element,querySelectorAll:()=>[]};
 const context={};vm.createContext(context);vm.runInContext(fs.readFileSync(path.join(root,'panel/ui/iz2sciv.js'),'utf8'),context);
 const state={preview,platform:{loaderId:'iz2sciv-core'},log:[],iz2sciv:{phase:'idle',busy:false,sdReady:true,phy:'phy0',setup:{supported:true,keysReady,dependenciesReady:true,capabilitiesReady:true,networkManager:true,radios:['phy0']},...initial}};
 const calls=[];let ui;
 ui=context.IZ2S_IZ2SCIV.create({document,isUnavailable,isPanelBusy,resetSession,getLink:()=>link,getFtp:()=>ftp,setPcLink:enabled=>calls.push(['pc-link',enabled]),closeFtp:()=>calls.push('ftp-close'),openFiles:()=>calls.push('pc-files'),openFtp:()=>calls.push('ftp-files'),openLibrary:()=>calls.push('library'),downloadSd:async()=>{},downloadWifiReport:report=>calls.push({report}),act:async(route,body,options)=>{
  calls.push(body);
  if(body.action==='reset-connection'){Object.assign(state.iz2sciv,{phase:'connection-reset',loaderReady:false,installed:false});ui.render(state);}
  if(body.action==='import-keys'){
   if(importError){options.onError(Error(importError));return;}
   state.iz2sciv.setup.keysReady=true;ui.render(state);
  }
  return {ok:true};
 }});
 ui.render(state);
 return {ui,state,calls,api:context.IZ2S_IZ2SCIV,$:id=>elements.get('iz2sciv-'+id)};
}
test('PC action advances only when keys, tools, permissions and selected radio are ready',()=>{
 const f=fixture(),s=f.state.iz2sciv;
 assert.equal(f.api.pcAction(s),'keys');s.setup.keysReady=true;s.setup.dependenciesReady=false;
 assert.equal(f.api.pcAction(s),'prepare');s.setup.dependenciesReady=true;s.setup.capabilitiesReady=false;
 assert.equal(f.api.pcAction(s),'permissions');s.setup.capabilitiesReady=true;s.setup.networkManager=false;
 assert.equal(f.api.pcAction(s),'refresh');s.setup.networkManager=true;s.setup.radios=[];
 assert.equal(f.api.pcAction(s),'refresh');s.setup.radios=['phy1','phy2'];
 assert.equal(f.api.pcAction(s),'radio');s.phy='phy2';assert.equal(f.api.pcAction(s),'continue');
 s.setup.supported=false;assert.equal(f.api.pcAction(s),'unsupported');
});
test('saved keys remain visibly verified after rendering and without a browser file selection',()=>{
 const f=fixture({keysReady:true});f.ui.render(f.state);
 assert.equal(f.$('keys-state').textContent,'Saved and verified');assert.equal(f.$('keys-file').textContent,'prod.keys');
 assert.equal(f.$('keys-replace').hidden,false);assert.equal(f.$('pc-action').textContent,'Continue to connection');
 f.$('pc-action').click();assert.equal(f.$('stage-3').hidden,false);assert.equal(f.calls.length,0);
});
test('successful key import keeps filename and confirmation when the file input is cleared',async()=>{
 const f=fixture();f.$('keys').files=[{name:'my-prod.keys',size:100,text:async()=>'test-only fixture'}];
 await f.$('keys').onchange();f.ui.render(f.state);
 assert.equal(f.$('keys').value,'');assert.equal(f.$('keys-file').textContent,'my-prod.keys');
 assert.equal(f.$('keys-state').textContent,'Saved and verified');assert.match(f.$('keys-feedback').textContent,/saved and verified/);
 assert.equal(f.calls.length,1);assert.equal(f.calls[0].action,'import-keys');
});
test('rejected replacement shows an inline error and retains the previous verified file',async()=>{
 const f=fixture({keysReady:true,importError:'Invalid prod.keys'});
 f.$('keys').files=[{name:'bad.keys',size:40,text:async()=>'invalid fixture'}];
 await f.$('keys').onchange();f.ui.render(f.state);
 assert.equal(f.$('keys-file').textContent,'prod.keys');assert.equal(f.$('keys-feedback').attributes.role,'alert');
 assert.match(f.$('keys-feedback').textContent,/previously saved file is still in use/);
 assert.equal(f.$('keys-state').textContent,'Saved and verified');
});
test('oversized key files are rejected before upload',async()=>{
 const f=fixture();f.$('keys').files=[{name:'huge.keys',size:65537,text:()=>{throw Error('must not read');}}];
 await f.$('keys').onchange();assert.equal(f.calls.length,0);assert.match(f.$('keys-feedback').textContent,/64 KB/);
 assert.equal(f.$('pc-action').textContent,'Choose prod.keys');
});
test('busy and preview states cannot start PC actions',()=>{
 const f=fixture({preview:true});f.$('pc-action').click();assert.equal(f.$('pc-action').disabled,true);assert.equal(f.calls.length,0);
 f.state.preview=false;f.state.iz2sciv.busy=true;f.ui.render(f.state);f.$('pc-action').click();assert.equal(f.calls.length,0);
});
test('summary hides dependency noise while full output preserves every event and error',()=>{
 const f=fixture();f.state.iz2sciv.error='Specific failure';
 f.state.log=[{message:'Collecting package'}, {message:'IZ2SCiv: prepare-done'}, {message:'Specific failure'}, {message:'CIV7_LDN_NETWORK_RESTORED'}];
 const summary=f.ui.activityLog(f.state,false);
 assert.equal(summary.length,3);assert.equal(summary[0].message,'PC dependencies prepared.');assert.equal(summary[1].message,'Specific failure');
 assert.equal(f.ui.activityLog(f.state,true),f.state.log);
});
test('summary keeps earlier failures and collapses consecutive payload progress',()=>{
 const f=fixture();f.state.log=[{message:'Error: Timed out'}, {message:'Sending payload: 0% (0/19 messages)'}, {message:'Sending payload: 50% (10/19 messages)'}, {message:'Sending payload: 100% (19/19 messages)'}, {message:'CIV7_LDN_NETWORK_RESTORED'}];
 const rows=f.ui.activityLog(f.state,false);
 assert.equal(rows.length,3);assert.equal(rows[0].message,'Error: Timed out');assert.equal(rows[1].message,'Sending initial loader: 100%');
 assert.equal(f.ui.activityLog(f.state,true).length,5);
});
test('one status card follows Wi-Fi progress on every step without claiming completion early',()=>{
 const f=fixture({keysReady:true}),s=f.state.iz2sciv;
 s.busy=true;s.phase='start';s.networkStage='restoring';s.progress={line:19,total:19};f.ui.render(f.state);
 for(const step of [1,2,3,5]){f.$('step-'+step).click();assert.equal(f.$('result').hidden,false);assert.equal(f.$('status').textContent,'Restoring PC Wi-Fi…');}
 assert.equal(f.$('progress').attributes.value,undefined);assert.equal(f.$('stop').hidden,false);
 s.busy=false;s.error='Network failed';s.networkRecoveryAvailable=true;f.ui.render(f.state);
 assert.equal(f.$('restore-network').hidden,false);assert.equal(f.$('result').dataset.tone,'error');assert.equal(f.$('install').disabled,true);
 f.state.platform.loaderId='iz2s-core';f.ui.render(f.state);assert.equal(f.$('result').hidden,true);assert.equal(f.$('flow').hidden,true);
});
test('connection has one route and requires the lobby confirmation',()=>{
 const f=fixture({keysReady:true});f.state.iz2sciv.prepared=true;f.$('step-3').click();
 assert.equal(f.$('start').disabled,true);
 f.$('lobby').checked=true;f.ui.render(f.state);
 assert.equal(f.$('start').disabled,false);
 assert.equal(f.$('route-loader'),undefined);
});
test('connection completion advances to installation even when a legacy save needs an update',()=>{
 const f=fixture();
 f.$('step-3').click();
 Object.assign(f.state.iz2sciv,{busy:true,phase:'start'});f.ui.render(f.state);
 assert.equal(f.$('connect-state').textContent,'Connecting to the console…');
 Object.assign(f.state.iz2sciv,{busy:false,phase:'start-done',needsSaveUpdate:true,loaderReady:true});
 f.ui.render(f.state);
 assert.equal(f.$('stage-4').hidden,false);
 assert.equal(f.$('result').hidden,false);
 assert.equal(f.$('status').textContent,'Waiting for installation confirmation.');
 assert.match(f.$('status-help').textContent,/Do not restart/);
 assert.equal(f.$('install').disabled,true);
 assert.equal(f.calls.length,0);
});
test('an obsolete save flag cannot claim a completed setup',()=>{
 const f=fixture();f.state.iz2sciv.needsSaveUpdate=true;f.state.iz2sciv.phase='install-done';f.ui.render(f.state);
 assert.equal(f.$('save-update'),undefined);assert.equal(f.$('installed-next').hidden,true);assert.equal(f.$('status').textContent,'Ready for the next step.');
 assert.equal(f.$('status-help').textContent,'Follow the connection steps below.');
 assert.equal(f.$('save-action-title').textContent,'Connect the console');
 f.$('view-run').click();assert.match(f.$('launch-note').textContent,/already installed/);
});
test('native installation progress is displayed separately from old LDN progress',()=>{
 const f=fixture();Object.assign(f.state.iz2sciv,{busy:true,phase:'install',operationProgress:{stage:'transferring',percent:50},progress:{line:19,total:19}});f.ui.render(f.state);
 assert.equal(f.$('progress').value,50);assert.equal(f.$('status').textContent,'Transferring the launch save: 50%');
});

test('preview shows the connection steps but disables console commands',()=>{
 const f=fixture({preview:true});f.$('step-3').click();assert.equal(f.$('stage-3').hidden,false);assert.equal(f.$('start').disabled,true);
});

test('full launch opens the combined connection and save stage with network settings',()=>{
 const f=fixture();f.$('view-run').click();f.$('run-full').click();assert(!f.$('result').hidden);assert(!f.$('network').hidden);assert(!f.$('stage-3').hidden);assert(!f.$('stage-4').hidden);assert(f.$('stage-5').hidden);
 assert(!html.includes('id="izcivswitch-info"'));assert(!html.includes('id="iz2sciv-launch"'));
});
test('live PC Link opens compact launcher tools and disconnect restores guidance',()=>{
 const link={online:true,files:true},f=fixture({link});
 assert.equal(f.$('library').disabled,false);assert.equal(f.$('files').disabled,false);assert.equal(f.$('launch-guide').hidden,true);assert.equal(f.$('flow').dataset.connected,'true');
 assert.equal(f.$('back-setup').hidden,true);
 link.online=false;f.ui.render(f.state);assert(f.$('library').disabled);assert.equal(f.$('files').disabled,false);assert.equal(f.$('launch-guide').hidden,false);
 assert.equal(f.$('back-setup').hidden,false);
});

test('PC Link pause and FTP controls stay independent during a network change',()=>{
 const link={online:true,files:true,enabled:true,service:{},consoleAddress:'10.42.0.20'};
 const ftp={enabled:false,online:false},f=fixture({link,ftp});
 assert.match(f.$('pc-status').textContent,/10.42.0.20/);
 f.$('pc-toggle').click();
 assert.deepEqual(f.calls.at(-1),['pc-link',false]);
 f.$('files').click();
 assert.equal(f.calls.at(-1),'pc-files');
 Object.assign(link,{enabled:false,online:false,files:false});
 f.ui.render(f.state);
 assert.equal(f.$('pc-toggle').textContent,'Resume PC Link');
 assert.equal(f.$('files').hidden,true);
 f.$('ftp-open').click();
 assert.equal(f.calls.at(-1),'ftp-files');
 Object.assign(ftp,{enabled:true,online:true,host:'192.168.1.20'});
 f.ui.render(f.state);
 assert.match(f.$('ftp-status').textContent,/192.168.1.20/);
 assert.equal(f.$('ftp-close').hidden,false);
 assert.equal(f.$('files').hidden,false);
 f.$('ftp-close').click();
 assert.equal(f.calls.at(-1),'ftp-close');
 f.$('pc-toggle').click();
 assert.deepEqual(f.calls.at(-1),['pc-link',true]);
 ftp.online=false;
 f.ui.render(f.state);
 assert.equal(f.$('ftp-open').textContent,'Reconnect FTP');
});
test('PC Link failure is visible and clears after recovery',()=>{
 const link={online:false,error:'PC Link belongs to another installation. Start the 0.3.0 PC Link service.'},f=fixture({link});
 assert.equal(f.$('link-state').textContent,'PC Link unavailable');assert.equal(f.$('link-error').hidden,false);
 assert.equal(f.$('link-error').textContent,link.error);assert.equal(f.$('library').disabled,true);
 delete link.error;link.online=true;f.ui.render(f.state);
 assert.equal(f.$('link-error').hidden,true);assert.equal(f.$('link-error').textContent,'');assert.equal(f.$('library').disabled,false);
});
test('PC Link reconnect keeps the selected setup step open',()=>{
 const link={online:true,files:true},f=fixture({link});
 link.online=false;f.ui.render(f.state);
 f.$('back-setup').click();
 f.$('step-2').click();
 link.online=true;f.ui.render(f.state);
 assert.equal(f.$('flow').dataset.view,'setup');
 assert.equal(f.$('step-2').attributes['aria-current'],'step');
 assert.equal(f.$('stage-2').hidden,false);
});
test('stale link status cannot enable tools while the local server is unavailable',()=>{
 let offline=false;
 const f=fixture({link:{online:true,files:true},isUnavailable:()=>offline});
 offline=true;f.ui.render(f.state);
 assert.equal(f.$('link-state').textContent,'Waiting for the local server…');
 for(const id of ['library','files','link-refresh'])assert.equal(f.$(id).disabled,true);
 offline=false;f.ui.render(f.state);
 assert.equal(f.$('link-state').textContent,'PC Link connected');
 for(const id of ['library','files','link-refresh'])assert.equal(f.$(id).disabled,false);
});
test('a diagnostic with no printable text does not break the setup screen',()=>{
 const f=fixture();
 f.state.iz2sciv.error=' \n'.repeat(200);
 assert.doesNotThrow(()=>f.ui.render(f.state));
 assert.equal(f.$('error').textContent,'Technical details are in Full output.');
});
test('session reset is blocked during transfers and reopens the first setup step on success',async()=>{
 const link={online:true,files:true,fileTransfers:true};
 let resets=0;
 const f=fixture({link,resetSession:async()=>{resets++;return {ok:true};}});
 f.state.iz2sciv.phase='confirm-sd-done';
 f.ui.render(f.state);
 await f.$('reset-session').click();
 assert.equal(resets,0);
 link.fileTransfers=false;f.ui.render(f.state);
 f.$('sd-copied').checked=true;
 await f.$('reset-session').click();
 assert.equal(resets,1);
 assert.equal(f.$('flow').dataset.view,'setup');
 assert.equal(f.$('stage-1').hidden,false);
 assert.equal(f.$('sd-copied').checked,false);
 f.$('sd-copied').checked=true;
 f.$('sd-copied').onchange();
 await f.$('confirm-sd').click();
 assert.equal(f.calls.at(-1).action,'confirm-sd');
 assert.equal(f.$('stage-2').hidden,false);
});
test('SD confirmation is available again when a separate panel operation ends',async()=>{
 let busy=true;
 const f=fixture({isPanelBusy:()=>busy});
 f.$('sd-copied').checked=true;
 f.$('sd-copied').onchange();
 assert.equal(f.$('confirm-sd').disabled,true);
 await f.$('confirm-sd').click();
 assert.equal(f.calls.length,0);
 busy=false;
 f.ui.render(f.state);
 assert.equal(f.$('confirm-sd').disabled,false);
 await f.$('confirm-sd').click();
 assert.equal(f.$('stage-2').hidden,false);
});

test('reopening the panel restores the actual operation step without sending anything',()=>{
 const cases=[
  [{phase:'idle',sdReady:false},1],
  [{phase:'confirm-sd-done'},2],
  [{phase:'idle',prepared:true},3],
  [{phase:'start',busy:true,installed:true},3],
  [{phase:'start-done',loaderReady:false},3],
  [{phase:'check-loader-done',loaderReady:true},4],
  [{phase:'install',busy:true},4],
  [{phase:'failed',lastAction:'install',error:'Connection interrupted'},4],
  [{phase:'interrupted',lastAction:'install'},4],
  [{phase:'install-done',installed:true},5],
 ];
 for(const [initial,step] of cases){
  const f=fixture({initial});
  assert.equal(f.$('stage-'+step).hidden,false,JSON.stringify(initial));
  assert.equal(f.calls.length,0);
 }
});
test('address verification, installation progress and confirmed completion advance in order',()=>{
 const f=fixture({initial:{prepared:true}}),s=f.state.iz2sciv;
 for(const phase of ['find-addresses-done','select-host-done','check-loader-done']){
  f.$('step-3').click();
  Object.assign(s,{id:phase,phase,loaderReady:true});f.ui.render(f.state);
  assert.equal(f.$('stage-4').hidden,false);
  assert.equal(f.calls.length,0);
 }
 Object.assign(s,{id:'install-1',phase:'install',busy:true});f.ui.render(f.state);
 assert.equal(f.$('stage-4').hidden,false);
 assert.equal(f.$('replace').disabled,true);
 Object.assign(s,{phase:'failed',busy:false,error:'Write failed'});f.ui.render(f.state);
 assert.equal(f.$('stage-4').hidden,false);
 assert.equal(f.$('stage-5').hidden,true);
 assert.equal(f.$('error').textContent,'Write failed');
 Object.assign(s,{id:'install-2',phase:'install',busy:true,error:null});f.ui.render(f.state);
 Object.assign(s,{phase:'install-done',busy:false,installed:true});f.ui.render(f.state);
 assert.equal(f.$('stage-5').hidden,false);
});
test('completed connection advances without an extra check or another lobby transfer',()=>{
 const f=fixture({initial:{phase:'start-done',loaderReady:true}});
 assert.equal(f.$('check-connection'),undefined);
 assert.equal(f.$('connection-resume'),undefined);
 f.$('step-3').click();
 assert.equal(f.$('stage-3').hidden,false);
 assert.equal(f.$('step-3').attributes['aria-current'],'step');
 assert.equal(f.$('stage-4').hidden,false);
 assert.equal(f.calls.length,0);
});
test('identical completion phase from a new attempt advances once and does not override browsing',()=>{
 const f=fixture({initial:{id:'first',phase:'start-done',loaderReady:true}});
 f.$('step-2').click();f.ui.render(f.state);
 assert.equal(f.$('stage-2').hidden,false);
 f.state.iz2sciv.id='second';f.ui.render(f.state);
 assert.equal(f.$('stage-4').hidden,false);
 f.$('step-3').click();f.ui.render(f.state);
 assert.equal(f.$('stage-3').hidden,false);
});
test('offline and network recovery block resuming a console connection',async()=>{
 let offline=true;
 const f=fixture({isUnavailable:()=>offline,initial:{phase:'start-done'}});
 assert.equal(f.$('install').disabled,true);
 offline=false;f.state.iz2sciv.networkRecoveryAvailable=true;f.ui.render(f.state);
 assert.equal(f.$('install').disabled,true);
 assert.equal(f.$('install').disabled,true);
});

test('a new connection cannot reuse an old installation success as its own result',()=>{
 const f=fixture({initial:{phase:'start-done',installed:true,loaderReady:true}});
 assert.equal(f.$('stage-4').hidden,false);
 assert.equal(f.$('installed-next').hidden,true);
 assert.equal(f.$('save-action-title').textContent,'Confirm installation below.');
 assert.equal(f.$('state-label').textContent,'Waiting for confirmation');
});

test('PC Continue and lobby checkbox never send; only confirmed Connect starts delivery',async()=>{
 const f=fixture({keysReady:true,initial:{prepared:true}});
 f.$('step-2').click();
 f.$('lobby').checked=true; // Stale consent from an earlier visit must not be reused.
 f.$('pc-action').click();
 assert.equal(f.$('stage-3').hidden,false);
 assert.equal(f.$('connection-preflight').hidden,false);
 assert.equal(f.$('lobby').checked,false);
 assert.equal(f.calls.length,0);
 await f.$('start').click();assert.equal(f.calls.length,0);
 f.$('lobby').checked=true;f.$('lobby').onchange();
 assert.equal(f.calls.length,0);
 await f.$('start').click();
 assert.equal(f.calls.length,1);assert.equal(f.calls[0].action,'start');assert.equal(f.calls[0].confirm,true);
 await f.$('start').click();assert.equal(f.calls.length,1);
});
test('loading and loaded states cannot ask for a fresh reboot or repeat lobby delivery',async()=>{
 const f=fixture({keysReady:true,initial:{prepared:true}}),s=f.state.iz2sciv;
 Object.assign(s,{phase:'start',busy:true});f.ui.render(f.state);
 assert.equal(f.$('connection-preflight').hidden,true);
 Object.assign(s,{phase:'start-done',busy:false,loaderReady:true});f.ui.render(f.state);
 assert.equal(f.$('stage-4').hidden,false);
 f.$('step-3').click();
 assert.equal(f.$('connection-preflight').hidden,true);
 f.$('lobby').checked=true;f.$('lobby').onchange();
 assert.equal(f.$('start').disabled,true);
 await f.$('start').click();assert.equal(f.calls.length,0);
});

test('a historical completion without a live result keeps the lobby available',()=>{
 const f=fixture({keysReady:true,initial:{phase:'start-done',loaderReady:false,prepared:true}});
 assert.equal(f.$('stage-3').hidden,false);
 assert.equal(f.$('connection-preflight').hidden,false);
 assert.equal(f.$('install').disabled,true);
 assert.doesNotMatch(f.$('status').textContent,/loader found|verified/i);
 assert.equal(f.calls.length,0);
});
test('removed loader can clear old success and restart only after fresh lobby consent',async()=>{
 const f=fixture({keysReady:true,initial:{phase:'start-done',loaderReady:true,prepared:true,installed:true}});
 f.$('step-3').click();f.$('lobby').checked=true;
 assert.equal(f.$('connection-reset').hidden,false);
 await f.$('connection-reset').click();
 assert.equal(f.$('stage-3').hidden,false);
 assert.equal(f.$('connection-preflight').hidden,false);
 assert.equal(f.$('install').disabled,true);
 assert.equal(f.$('lobby').checked,false);
 assert.equal(f.$('start').disabled,true);
 assert.deepEqual(f.calls.map(c=>c.action),['reset-connection']);
 f.$('lobby').checked=true;f.$('lobby').onchange();await f.$('start').click();
 assert.deepEqual(f.calls.map(c=>c.action),['reset-connection','start']);
});
test('connection reset is blocked while busy, offline or in preview',async()=>{
 for(const options of [{preview:true},{isUnavailable:()=>true},{initial:{busy:true,phase:'start'}}]){
  const f=fixture(options);await f.$('connection-reset').click();assert.equal(f.calls.length,0);
 }
});

test('full launch ignores previous boot success but keeps launch instructions accessible',async()=>{
 const f=fixture({keysReady:true,initial:{installed:true,loaderReady:true,phase:'install-done',prepared:true}});
 f.$('run-full').click();
 assert.equal(f.$('stage-3').hidden,false);
 assert.equal(f.$('step-1').parentElement.hidden,true);
 assert.equal(f.$('step-2').parentElement.hidden,true);
 assert.equal(f.$('step-3').textContent,'1. Connect and install save');
 assert.equal(f.$('step-5').textContent,'2. Launch IZ2S');
 assert.equal(f.$('step-5').disabled,false);
 assert.equal(f.$('connection-preflight').hidden,false);
 assert.equal(f.$('previous-launcher').hidden,true);
 assert.equal(f.calls.length,0);
 f.$('step-5').click();assert.equal(f.$('stage-5').hidden,false);
 assert.match(f.$('launch-note').textContent,/instructions at any time/);
 f.$('step-3').click();
 f.$('lobby').checked=true;f.$('lobby').onchange();await f.$('start').click();
 assert.equal(f.calls[0].action,'start');
 Object.assign(f.state.iz2sciv,{id:'new-boot',phase:'start',busy:true});f.ui.render(f.state);
 assert.equal(f.$('view-run').disabled,true);
 Object.assign(f.state.iz2sciv,{phase:'start-done',busy:false,loaderReady:true});f.ui.render(f.state);
 assert.equal(f.$('stage-3').hidden,false);assert.equal(f.$('stage-4').hidden,false);
 assert.equal(f.$('step-3').attributes['aria-current'],'step');
 assert.equal(f.$('connection-preflight').hidden,true);
 assert.equal(f.$('replace').disabled,false);
 f.$('replace').checked=true;f.$('replace').onchange();await f.$('install').click();
 assert.equal(f.calls[1].action,'install');
 Object.assign(f.state.iz2sciv,{phase:'install',busy:true});f.ui.render(f.state);
 Object.assign(f.state.iz2sciv,{phase:'install-done',busy:false,installed:true});f.ui.render(f.state);
 assert.equal(f.$('stage-5').hidden,false);
 f.$('restart-run').click();
 assert.equal(f.$('stage-3').hidden,false);assert.equal(f.$('lobby').checked,false);
 assert.equal(f.$('replace').checked,false);assert.equal(f.$('step-5').disabled,false);
 assert.equal(f.calls.length,2);
});
test('first setup has four navigation steps and exposes save only after connection',()=>{
 const f=fixture({keysReady:true,initial:{prepared:true}});
 f.$('step-3').click();
 assert.equal(f.$('step-4'),undefined);
 assert.equal(f.$('step-3').textContent,'3. Connect and install save');
 assert.equal(f.$('step-5').textContent,'4. Launch IZ2S');
 assert.equal(f.$('stage-3').hidden,false);assert.equal(f.$('stage-4').hidden,false);
 assert.equal(f.$('install').disabled,true);assert.equal(f.$('save-instructions').hidden,true);
 assert.match(f.$('save-action-help').textContent,/connection above/);
});

test('switching to setup cannot restore connection proof from the previous boot',()=>{
 const f=fixture({keysReady:true,initial:{installed:true,loaderReady:true,phase:'install-done',prepared:true}});
 f.$('view-setup').click();f.$('step-3').click();
 assert.equal(f.$('connection-preflight').hidden,false);
 assert.equal(f.$('replace').disabled,true);
 assert.equal(f.$('step-5').disabled,true);
 assert.equal(f.calls.length,0);
});


test('quick launch and route switches only show instructions and never send commands',async()=>{
 const f=fixture({initial:{prepared:true}});
 f.$('view-run').click();
 assert.equal(f.$('stage-5').hidden,false);
 assert.equal(f.$('run-quick').attributes['aria-pressed'],'true');
 assert.equal(f.$('step-5').disabled,false);
 assert.match(f.$('launch-note').textContent,/already installed/);
 f.$('run-full').click();
 assert.equal(f.$('stage-3').hidden,false);
 assert.equal(f.$('run-full').attributes['aria-pressed'],'true');
 assert.equal(f.$('install').disabled,true);
 f.$('run-quick').click();
 assert.equal(f.$('stage-5').hidden,false);
 assert.equal(f.$('install').disabled,true);
 assert.equal(f.calls.length,0);
});
test('running operations prevent route changes and cannot be bypassed by launch instructions',()=>{
 const f=fixture({initial:{busy:true,phase:'start'}});
 for(const id of ['run-quick','run-full','step-5']){
  assert.equal(f.$(id).disabled,true);f.$(id).click();
  assert.equal(f.$('stage-3').hidden,false);
 }
 assert.equal(f.calls.length,0);
});

test('Windows network setup bypasses Linux keys and exposes loader verification without LDN delivery',async()=>{
 const f=fixture({initial:{hostPlatform:'win32',prepared:false,setup:{supported:false},windowsWifi:{adapters:[{name:'Wireless',description:'USB Wi-Fi'}]}}});
 assert.equal(f.$('windows').hidden,false);
 assert.equal(f.$('linux-checks').hidden,true);
 assert.equal(f.$('pc-action').disabled,false);
 assert.match(f.$('windows-adapters').textContent,/USB Wi-Fi/);
 f.$('pc-action').click();
 assert.equal(f.$('stage-3').hidden,false);
 assert.equal(f.$('connection-preflight').hidden,true);
 assert.equal(f.$('start').disabled,true);
 await f.$('windows-find').click();
 assert.deepEqual(f.calls.map(x=>x.action),['check-loader']);
});

test('Windows inspection failure is distinct from no adapter and is included in the downloadable report',async()=>{
 const wifi={checkedAt:'2026-11-22T00:00:00Z',status:'inspection-failed',adapters:[],error:'CIM failed'};
 const f=fixture({initial:{hostPlatform:'win32',windowsWifi:wifi}});
 assert.match(f.$('windows-status').textContent,/inspection failed/);
 assert.equal(f.$('windows-adapters').textContent,'');assert.equal(f.$('windows-error').textContent,'CIM failed');
 f.$('windows-report').click();assert.equal(f.calls[0].report,wifi);
 await f.$('windows-refresh').click();assert.equal(f.calls[1].action,'wifi-status');
});
test('Windows driver evidence stays unverified for LDN, survives polling, and updates after unplugging',()=>{
 const f=fixture({initial:{hostPlatform:'win32',windowsWifi:{checkedAt:'now',status:'ldn-unavailable',adapters:[{
  id:'one',name:'Laptop Wi-Fi',description:'<script>example</script>',phyTypes:['802.11ax'],
  monitor:{status:'advertised'},compatibility:{ordinaryWifi:true,reasons:[]}
 }]}}});
 assert.match(f.$('windows-status').textContent,/not yet verified/);
 const details=f.$('windows-checks').children[0];details.open=true;
 assert(details.children.some(p=>/reception not tested/.test(p.textContent)));
 assert(details.children.some(p=>p.textContent==='<script>example</script>'));
 f.ui.render(f.state);assert.equal(f.$('windows-checks').children[0],details);assert.equal(details.open,true);
 f.state.iz2sciv.windowsWifi={checkedAt:'later',status:'no-adapter',adapters:[]};f.ui.render(f.state);
 assert.equal(f.$('windows-checks').children.length,0);assert.match(f.$('windows-status').textContent,/does not see/);
 assert.equal(f.$('start').disabled,true);
});
