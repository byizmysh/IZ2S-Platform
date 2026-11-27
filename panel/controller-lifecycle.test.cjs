'use strict';
const test=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),vm=require('node:vm');
const {createRequire}=require('node:module');
test('Civ7 polling keeps one session and rejects removed entry points',async()=>{
 const file=path.join(__dirname,'controller.cjs'),realRequire=createRequire(file);let sessions=0,selected,sessionOptions;const writes=[];
 const session={ownsListener:()=>false,action:async()=>({ok:true}),refresh:async()=>({phase:'install-done'})};
 const mocks={
 '../core/session/ledger.cjs':{readJson:async()=>({loaderId:'iz2s-core',variant:'izlauncher'}),saveJson:async(file,value)=>writes.push(value)},
 '../engines/index.cjs':{create:()=>({select:id=>{selected=id;return {};}})},
 '../engines/iz2sciv-core/session.cjs':{create:options=>{sessions++;sessionOptions=options;return session;}},
 '../core/packages/repository.cjs':{create:async()=>({})},
 '../core/session/connections.cjs':{createConnections:async()=>({})}
 };
 const exported={exports:{}};
 vm.runInThisContext('(function(require,module,exports){'+fs.readFileSync(file,'utf8')+'\n})',{filename:file})(name=>{
  assert(!name.includes('/hardware/'),'Civ7 must not initialize a removed input device');
  return Object.hasOwn(mocks,name)?mocks[name]:realRequire(name);
 },exported,exported.exports);
 const manager=await exported.exports.create({root:'/fake',settings:{consolePort:39380},node:process.execPath});
 assert.equal(selected,'iz2sciv-core');assert.equal(sessionOptions.settings.consolePort,9026);
 assert.equal(writes[0].loaderId,'iz2sciv-core');
 await manager.iz2scivAction({action:'install'});
 for(let i=0;i<3;i++)assert.equal((await manager.refresh(true)).phase,'install-done');
 assert.equal(sessions,1);
 await assert.rejects(manager.configureLaunch({loaderId:'iz2s-core'}),/Civilization VII only/);
 await assert.rejects(manager.configureLaunch({configurationId:'pc-switch1'}),/Civilization VII only/);
 assert.deepEqual(await manager.configureLaunch({loaderId:'iz2sciv-core',variant:'original',configurationId:'pc-civ7'}),{ok:true});
});
