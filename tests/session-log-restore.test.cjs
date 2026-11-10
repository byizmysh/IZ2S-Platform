'use strict';
const {test}=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs/promises'),path=require('node:path'),os=require('node:os');
const {createLedger}=require('../core/session/ledger.cjs'),{createSessionManager}=require('../core/session/session.cjs');
async function fixture(t,status='stopped'){
 const root=await fs.mkdtemp(path.join(os.tmpdir(),'iz2s-log-restore-'));t.after(()=>fs.rm(root,{recursive:true,force:true}));
 const journal=createLedger(root),id='12345678-1234-1234-1234-123456789abc',runDirectory=path.join(root,'data/sessions/launcher-Fixture');
 const row={id,kind:'launch',status,created:'2026-11-07T23:20:02.000Z',completed:'2026-11-07T23:20:48.500Z',stopRequestedAt:'2026-11-07T23:20:48.000Z',needsRestart:true,runDirectory};
 await journal.write(row);await journal.append(id,'detail','2/4 Acquiring FS memory');
 await fs.mkdir(path.join(runDirectory,'fs-rpc/switch2'),{recursive:true});
 await fs.writeFile(path.join(runDirectory,'fs-rpc/switch2/pipeline.json'),JSON.stringify({error:'Error: Local native RPC connection failed: socket hang up\nstack',finished:'2026-11-07T23:20:48.600Z'}));
 let commands=0;
 const manager=createSessionManager({root,settings:{},node:process.execPath,arm:{halt:async()=>{commands++;}},devices:{inspect:async()=>({ready:true,items:[]})},sourceChecks:{readPlan:async()=>({})},readBootstrapProfile:async()=>({recovery:false})});
 return {root,id,journal,manager,commands:()=>commands};
}
test('a panel restart restores the latest attempt and the late FS error without issuing console commands',async t=>{
 const f=await fixture(t),state=await f.manager.snapshot();
 assert.equal(state.active,null);assert.equal(state.lastAttempt.id,f.id);assert.equal(state.lastAttempt.restoredFromDisk,true);
 assert(state.log.some(event=>event.type==='error'&&event.message.includes('socket hang up')));
 assert(state.log.some(event=>event.type==='warning'&&event.message.includes('after the PC session was stopped')));
 assert.equal(f.commands(),0);
});
test('full log download includes child diagnostics and does not duplicate an existing error',async t=>{
 const f=await fixture(t),first=await f.manager.historyLog(f.id),error=first.find(event=>event.source==='fs-rpc-report');
 assert(error);await f.journal.append(f.id,'error',error.message);
 const second=await f.manager.historyLog(f.id);assert.equal(second.filter(event=>event.message===error.message).length,1);
});
test('an interrupted historical attempt is never resumed as a live session',async t=>{
 const f=await fixture(t,'starting'),state=await f.manager.snapshot();
 assert.equal(state.active,null);assert.equal(state.lastAttempt.status,'failed');assert.equal(state.lastAttempt.needsRestart,true);assert.equal(f.commands(),0);
});

test('changing games can check session ownership without scanning the application library',async t=>{
 const f=await fixture(t),registry=require('../core/modules.cjs'),original=registry.list;
 registry.list=async()=>{throw Error('application catalog must not be read');};
 try{const state=await f.manager.snapshot({statusOnly:true});assert.equal(state.active,null);assert.equal(state.emergencyInputPending,false);assert.equal(state.changing,false);assert.equal(state.library,undefined);}finally{registry.list=original;}
 assert.equal(f.commands(),0);
});

test('terminal failures stay in history without recreating a live notification on panel initialization',async t=>{
 const f=await fixture(t,'failed'),message='FS RPC memory acquisition failed: native execution did not finish reliably. Resources retained; automatic retry disabled';
 const row=await f.journal.read(f.id);row.error=message;await f.journal.write(row);
 // The fixture manager may already have started its initial read. A new manager
 // models reopening the panel after this failed attempt was durably recorded.
 await f.manager.snapshot({statusOnly:true});
 const reopened=createSessionManager({root:f.root,settings:{},node:process.execPath,arm:{halt:async()=>{throw Error('History must never issue input');}},devices:{inspect:async()=>({ready:true,items:[]})},sourceChecks:{readPlan:async()=>({})},readBootstrapProfile:async()=>({recovery:false})});
 const state=await reopened.snapshot();
 assert.equal(state.active,null);assert.equal(state.notification,null);assert.equal(state.lastAttempt.error,message);
 assert.equal(state.lastAttempt.needsRestart,true);assert.equal((await f.journal.read(f.id)).error,message);
});
test('a PC process interrupted during a live attempt still reports the unresolved restart requirement',async t=>{
 const f=await fixture(t,'starting'),state=await f.manager.snapshot();
 assert.equal(state.notification.type,'warning');assert.match(state.notification.message,/previous PC server stopped/);
});
