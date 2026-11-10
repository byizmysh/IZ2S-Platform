'use strict';
const test=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs/promises'),path=require('node:path'),os=require('node:os');
const {createLedger}=require('../core/session/ledger.cjs'),{createSessionManager}=require('../core/session/session.cjs');
test('completed failed/disconnected attempts keep history but expose no old live connection or SD services',async t=>{
 const root=await fs.mkdtemp(path.join(os.tmpdir(),'iz-relaunch-state-'));t.after(()=>fs.rm(root,{recursive:true,force:true}));
 const journal=createLedger(root),id='12345678-1234-1234-1234-123456789abc',directory=path.join(root,'data/sessions/launcher-Previous');
 await fs.mkdir(directory,{recursive:true});
 await journal.write({id,kind:'launch',status:'failed',created:new Date().toISOString(),runDirectory:directory,error:'Old transport lost',needsRestart:true});
 await fs.writeFile(path.join(directory,'connection-state.json'),JSON.stringify({state:'connected',protocol:3,recoveryAvailable:true}));
 await fs.writeFile(path.join(directory,'run11-runtime.json'),JSON.stringify({menuReady:true,updated:new Date().toISOString(),sd:{state:2,active:0,presentOnCard:[{id:'old-app'}]},sdStorage:{enabled:true,state:'ready'},available:[{id:'old-app'}],memory:{bytes:1234}}));
 await fs.writeFile(path.join(directory,'console-state.json'),JSON.stringify({at:new Date().toISOString(),network:{connected:true}}));
 let inputs=0;const manager=createSessionManager({root,settings:{},node:process.execPath,arm:{halt:async()=>inputs++},devices:{inspect:async()=>({ready:true,items:[]})},sourceChecks:{readPlan:async()=>({})},readBootstrapProfile:async()=>({recovery:false})});
 const state=await manager.snapshot();assert.equal(state.active,null);assert.equal(state.connected,false);assert.equal(state.link,null);assert.equal(state.consoleState,null);assert.equal(state.applicationRecovery,null);assert.equal(state.sdStorage,null);assert.equal(state.sdRuntime,null);assert.equal(state.residentMemory,null);assert.equal(state.streamUrl,null);assert.deepEqual(state.sessionCatalog,[]);assert.deepEqual(state.sdInstalled,[]);assert.equal(state.lastAttempt.id,id);assert.equal(state.lastAttempt.error,'Old transport lost');assert.equal(state.notification,null);assert.equal(inputs,0);
});
for(const engine of ['izmysh','BermTheSequel']){
 const heap=require('../engines/'+engine+'/upstream/engine/switch2/memory/heap-state.cjs');
 test(engine+': reject the captured corrupt counter before rounding or reserving another group',()=>{
  const bytes=Buffer.alloc(128);bytes.writeBigUInt64LE(3505504n,40);assert.equal(heap.counters(bytes)[5],3505504);
  const corrupt=0x24242424245937f0n;bytes.writeBigUInt64LE(corrupt,40);
  assert.throws(()=>heap.counters(bytes),e=>e.heapStateUncertain&&e.resourcesRetained&&e.counterIndex===5&&e.counterValue===corrupt.toString());
  assert.throws(()=>heap.counters(Buffer.alloc(127)),e=>e.heapStateUncertain);
 });
 test(engine+': reject drift during a positive control even before a cache candidate exists',()=>{
  heap.assertRestored({profile:'positive',restored:true,baseline:3491888,after:3491888});
  assert.throws(()=>heap.assertRestored({profile:'positive',restored:false,baseline:3491888,after:3492000}),e=>e.heapStateUncertain&&e.resourcesRetained&&e.heapDrift.difference===112);
 });
}
test('native heap failure is reported as this attempt rather than a generic old-session block',async t=>{
 const root=await fs.mkdtemp(path.join(os.tmpdir(),'iz-heap-failure-'));t.after(()=>fs.rm(root,{recursive:true,force:true}));
 const directory=path.join(root,'launcher-Fresh');await fs.mkdir(path.join(directory,'fs-rpc/switch2'),{recursive:true});
 await fs.writeFile(path.join(directory,'fs-rpc-state.json'),JSON.stringify({state:'failed',resourcesRetained:true}));
 await fs.writeFile(path.join(directory,'fs-rpc/switch2/acquisition-supervisor.json'),JSON.stringify({session:'launcher-Fresh',resourcesRetained:true,error:'Native allocation counter changed unexpectedly; restart the game before another launch',attempts:[]}));
 const result=await require('../core/session/failure-report.cjs').inspect(directory,'host exited');assert.match(result.message,/during this attempt/);assert.equal(result.resourcesRetained,true);
});
