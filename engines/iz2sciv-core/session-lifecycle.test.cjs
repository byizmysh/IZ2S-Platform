'use strict';
const test=require('node:test'),assert=require('node:assert/strict');
const fs=require('node:fs/promises'),os=require('node:os'),path=require('node:path');
const {EventEmitter}=require('node:events'),{PassThrough}=require('node:stream');
const {create}=require('./session.cjs');
const deferred=()=>{let resolve;const promise=new Promise(r=>{resolve=r;});return {promise,resolve};};
async function fixture(t,{output='Installed. Restart the game, then open Load Game menu.',code=0,holdAction}={}){
 const root=await fs.mkdtemp(path.join(os.tmpdir(),'iz2sciv-session-test-'));
 t.after(()=>fs.rm(root,{recursive:true,force:true}));
 await fs.mkdir(path.join(root,'data/state'),{recursive:true});
 await fs.mkdir(path.join(root,'engines/iz2s-core/runtime'),{recursive:true});
 const records=['bootstrap.bin','ro/main','core/main','game/main','iz2s/izlauncher.iza','iz2s/izlauncher.romfs']
  .map((path,index)=>({path,bytes:100+index,sha256:String(index).repeat(64)}));
 await fs.writeFile(path.join(root,'engines/iz2s-core/runtime/package-manifest.json'),JSON.stringify(records));
 await fs.mkdir(path.join(root,'engines/iz2sciv-core'),{recursive:true});
 await fs.writeFile(path.join(root,'engines/iz2sciv-core/boot-revision.json'),JSON.stringify({revision:'test-save-boot-v1'}));
 await fs.writeFile(path.join(root,'data/state/iz2sciv.json'),JSON.stringify({phase:'idle',sdReady:true}));
 let launches=0,handle;
 const release=deferred();
 const ldn={action:async body=>{if(holdAction&&body.action==='import-keys')await holdAction.promise;},waitForOperation:async()=>{},
  snapshot:async()=>({civswitchLdn:{loaderReady:true,suggestedHost:'10.42.0.20',prepared:true,setup:{}},log:[],connections:{}}),
  stop:async()=>{},refresh:async()=>{}};
 const options={root,ldn,addressOptions:async()=>[],launch:()=>{
  launches++;handle=new EventEmitter();handle.stdout=new PassThrough();handle.stderr=new PassThrough();handle.kill=()=>{};
  release.promise.then(()=>{handle.stdout.write(output+'\n');handle.emit('close',code);});return handle;
 }};
 return {root,options,session:create(options),release,launches:()=>launches};
}
test('a request owns the session before a child operation exists',async t=>{
 const hold=deferred(),f=await fixture(t,{holdAction:hold});
 const pending=f.session.action({action:'import-keys',content:'test'});
 assert.equal(f.session.ownsListener(),true);
 const state=await f.session.snapshot();
 assert.equal(state.iz2sciv.busy,true);assert.equal(state.connections.locked,true);assert.ok(state.active);
 await assert.rejects(f.session.action({action:'refresh'}),/Wait for/);
 hold.resolve();await pending;assert.equal(f.session.ownsListener(),false);
});
test('polling during installation preserves ownership and confirmed success survives reload',async t=>{
 const f=await fixture(t);await f.session.action({action:'install',replaceTrigger:true});
 while(!f.launches())await new Promise(r=>setImmediate(r));
 for(let i=0;i<5;i++){
  await f.session.refresh(true);
  const state=await f.session.snapshot();assert.ok(state.active);assert.equal(state.iz2sciv.busy,true);
  assert.equal(state.iz2sciv.phase,'install');assert.equal(f.session.ownsListener(),true);
 }
 f.release.resolve();await f.session.waitForOperation();
 const state=await f.session.snapshot();
 assert.equal(state.iz2sciv.phase,'install-done');assert.equal(state.iz2sciv.installed,true);
 assert.equal(state.lastAttempt.status,'complete');assert.equal(state.iz2sciv.error,undefined);
 assert.equal(state.active,null);assert.equal(f.session.ownsListener(),false);
 const reloaded=await create(f.options).snapshot();
 assert.equal(reloaded.iz2sciv.phase,'install-done');assert.equal(reloaded.iz2sciv.installed,true);
 assert.equal(reloaded.lastAttempt.status,'complete');assert.equal(reloaded.iz2sciv.error,undefined);
 assert.equal(f.launches(),1);
});
test('missing completion confirmation remains an installation error',async t=>{
 const f=await fixture(t,{output:'Transfer started'});
 await f.session.action({action:'install',replaceTrigger:true});f.release.resolve();await f.session.waitForOperation();
 const state=await f.session.snapshot();assert.equal(state.iz2sciv.phase,'failed');assert.equal(state.iz2sciv.installed,false);
 assert.match(state.iz2sciv.error,/completion was not confirmed/);assert.equal(state.lastAttempt.status,'failed');
});
