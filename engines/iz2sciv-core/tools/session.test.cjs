'use strict';
const {test}=require('node:test'),assert=require('node:assert/strict');
const fs=require('node:fs/promises'),os=require('node:os'),path=require('node:path');
const {EventEmitter}=require('node:events'),{PassThrough}=require('node:stream');
const {create}=require('../session.cjs');
async function fixture(t,{badConnection=false,badRecheck=false,badMarker=false,hold='',delayInstall=false}={}){
 const root=await fs.mkdtemp(path.join(os.tmpdir(),'iz2sciv-flow-'));t.after(()=>fs.rm(root,{recursive:true,force:true}));
 const runtimeDir=path.join(root,'engines/iz2s-core/runtime');await fs.mkdir(runtimeDir,{recursive:true});
 const records=['bootstrap.bin','ro/main','core/main','game/main','iz2s/izlauncher.iza','iz2s/izlauncher.romfs'].map((path,i)=>({path,bytes:100+i,sha256:String(i).repeat(64)}));
 const writeRecords=()=>fs.writeFile(path.join(runtimeDir,'package-manifest.json'),JSON.stringify(records));
 await writeRecords();
 const engineDir=path.join(root,'engines/iz2sciv-core');await fs.mkdir(engineDir,{recursive:true});
 const writeBootRevision=revision=>fs.writeFile(path.join(engineDir,'boot-revision.json'),JSON.stringify({revision}));
 await writeBootRevision('run33-awaited-boot-v1');
 const calls=[],spawns=[];let stopped=false,release,installRelease;
 let state={loaderReady:false,suggestedHost:'10.42.0.20',setup:{},prepared:true};
 const gate=new Promise(r=>release=r);
 const ldn={async action(body){calls.push(body);if(body.action===hold)await gate;
  state={...state,loaderReady:!stopped&&!(body.action==='start'?badConnection:badRecheck)};
 },async waitForOperation(){},async snapshot(){return {civswitchLdn:state,log:[],connections:{}};},async stop(){stopped=true;release();},async refresh(){}};
 const launch=(program,args)=>{
  spawns.push({program,args});const p=new EventEmitter();p.stdout=new PassThrough();p.stderr=new PassThrough();
  let closed=false;const finish=(code,text='')=>{if(closed)return;closed=true;p.stdout.write(text);p.emit('close',code);};
  p.kill=()=>{setImmediate(()=>finish(143));return true;};
  installRelease=()=>finish(0,badMarker?'Disconnected before acknowledgement':'Installed. Restart the game, then open Load Game menu.');
  if(!delayInstall)setImmediate(installRelease);return p;
 };
 const session=create({root,ldn,launch,addressOptions:async()=>[]});
 await session.action({action:'confirm-sd',copied:true});
 return {session,calls,spawns,release,root,records,writeRecords,writeBootRevision,finishInstall:()=>installRelease?.(),start:()=>session.action({action:'start',confirm:true,installAfterSuccess:true}),result:async()=>{await session.waitForOperation();return (await session.snapshot()).iz2sciv;}};
}
test('one connect action rechecks the loader and installs exactly once',async t=>{
 const f=await fixture(t);await f.start();const s=await f.result();
 assert.deepEqual(f.calls.map(x=>x.action),['start','check-loader']);assert.equal(f.calls[0].installAfterSuccess,false);
 assert.equal(f.spawns.length,1);assert.equal(f.spawns[0].args.at(-1),'10.42.0.20');assert(f.spawns[0].args.includes('--chain'));
 assert.equal(s.phase,'install-done');assert.equal(s.installed,true);
 for(let i=0;i<3;i++)await f.session.snapshot();assert.equal(f.spawns.length,1,'poll/reload must not replay installation');
});
for(const mode of ['badConnection','badRecheck'])test(mode+' prevents any save write',async t=>{
 const f=await fixture(t,{[mode]:true});await f.start();const s=await f.result();assert.equal(f.spawns.length,0);assert.equal(s.installed,false);assert.equal(s.phase,'failed');
});
for(const hold of ['start','check-loader'])test('stop during '+hold+' prevents installation',async t=>{
 const f=await fixture(t,{hold});await f.start();while(!f.calls.some(c=>c.action===hold))await new Promise(r=>setImmediate(r));
 await assert.rejects(f.start(),/current operation/);await f.session.stop();const s=await f.result();assert.equal(s.phase,'interrupted');assert.equal(f.spawns.length,0);assert.equal(s.installed,false);
});
test('missing console acknowledgement is a failure; explicit retry rechecks loader',async t=>{
 const f=await fixture(t,{badMarker:true});await f.start();assert.equal((await f.result()).installed,false);
 await assert.rejects(f.session.action({action:'install'}),/Confirm installation/);
 await f.session.action({action:'install',replaceTrigger:true});const s=await f.result();assert.equal(f.spawns.length,2);assert.equal(f.calls.at(-1).action,'check-loader');assert.equal(s.phase,'failed');assert.match(s.error,/completion was not confirmed/);
});
test('stop during save transfer never reports success',async t=>{
 const f=await fixture(t,{delayInstall:true});await f.start();while(!f.spawns.length)await new Promise(r=>setImmediate(r));
 await f.session.stop();assert.equal((await f.result()).installed,false);assert.equal((await f.result()).phase,'interrupted');
});
test('old two-step start request is rejected before transport',async t=>{
 const f=await fixture(t);await assert.rejects(f.session.action({action:'start',confirm:true}),/Connect and install/);assert.equal(f.calls.length,0);
});

test('a changed boot core invalidates the remembered installed save',async t=>{
 const f=await fixture(t);await f.start();assert.equal((await f.result()).installed,true);
 f.records[0].sha256='f'.repeat(64);await f.writeRecords();
 const s=(await f.session.snapshot()).iz2sciv;assert.equal(s.installed,false);assert.equal(s.needsSaveUpdate,true);
});

test('launcher, menu and ROMFS updates do not invalidate a flexible save',async t=>{
 const f=await fixture(t);await f.start();const installed=await f.result();
 for(const record of f.records.slice(3)){record.bytes+=77;record.sha256='a'.repeat(64);}
 f.records.reverse();await f.writeRecords();
 const s=(await f.session.snapshot()).iz2sciv;
 assert.equal(s.installed,true);assert.equal(s.needsSaveUpdate,false);
 assert.equal(s.saveCompatibilityFingerprint,installed.saveCompatibilityFingerprint);
 assert.equal(s.runtimeFingerprint,installed.runtimeFingerprint,'keep the original installation receipt');
});

test('legacy exact-size save needs a one-time policy upgrade even with identical runtime',async t=>{
 const f=await fixture(t);await f.start();await f.result();
 const file=path.join(f.root,'data/state/iz2sciv.json'),saved=JSON.parse(await fs.readFile(file));
 delete saved.saveCompatibilityFingerprint;await fs.writeFile(file,JSON.stringify(saved));
 const reopened=create({root:f.root,ldn:{snapshot:async()=>({civswitchLdn:{},log:[],connections:{}})}});
 const s=(await reopened.snapshot()).iz2sciv;assert.equal(s.installed,false);assert.equal(s.needsSaveUpdate,true);
});

test('even a mutable runtime change during installation fails the full race check',async t=>{
 const f=await fixture(t,{delayInstall:true});await f.start();while(!f.spawns.length)await new Promise(r=>setImmediate(r));
 f.records[3].bytes++;await f.writeRecords();f.finishInstall();
 const s=await f.result();assert.equal(s.installed,false);assert.equal(s.phase,'failed');
 assert.match(s.error,/runtime changed during installation/);
});

test('a saved boot revision change requires installation even with the same SD runtime',async t=>{
 const f=await fixture(t);await f.start();const installed=await f.result();
 assert.equal(installed.saveBootRevision,'run33-awaited-boot-v1');
 await f.writeBootRevision('next-boot-test');
 const s=(await f.session.snapshot()).iz2sciv;
 assert.equal(s.installed,false);assert.equal(s.needsSaveUpdate,true);
 assert.equal(s.saveBootRevision,installed.saveBootRevision,'preserve the actual installation receipt');
 assert.equal(s.expectedSaveBootRevision,'next-boot-test');
 await f.session.action({action:'install',replaceTrigger:true});
 const updated=await f.result();assert.equal(updated.installed,true);assert.equal(updated.needsSaveUpdate,false);
 assert.equal(updated.saveBootRevision,'next-boot-test');
});

test('a legacy receipt without a boot revision cannot claim that the corrected save is installed',async t=>{
 const f=await fixture(t);await f.start();await f.result();
 const file=path.join(f.root,'data/state/iz2sciv.json'),saved=JSON.parse(await fs.readFile(file));
 delete saved.saveBootRevision;await fs.writeFile(file,JSON.stringify(saved));
 const reopened=create({root:f.root,ldn:{snapshot:async()=>({civswitchLdn:{},log:[],connections:{}})}});
 const s=(await reopened.snapshot()).iz2sciv;assert.equal(s.installed,false);assert.equal(s.needsSaveUpdate,true);
});

test('a boot revision changed during transfer cannot be recorded as installed',async t=>{
 const f=await fixture(t,{delayInstall:true});await f.start();while(!f.spawns.length)await new Promise(r=>setImmediate(r));
 await f.writeBootRevision('next-boot-test');f.finishInstall();
 const s=await f.result();assert.equal(s.installed,false);assert.equal(s.phase,'failed');
 assert.match(s.error,/save revision changed during installation/);
});
