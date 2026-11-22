'use strict';
const test=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),vm=require('node:vm');
const {createRequire}=require('node:module');
test('Civ7 polling preserves the session when the unrelated SB4 keyboard is unpaired',async()=>{
 const file=path.join(__dirname,'controller.cjs'),realRequire=createRequire(file);
 let sessions=0,pairingChecks=0,current;
 const mocks={
  '../core/session/ledger.cjs':{readJson:async()=>({loaderId:'iz2sciv-core',variant:'original'}),saveJson:async()=>{}},
  '../engines/index.cjs':{create:()=>({list:()=>[{id:'iz2sciv-core'}],select:()=>({manifest:{installed:true},folder:'/fake/civ7'})})},
  '../core/packages/repository.cjs':{create:async()=>({})},
  '../hardware/profiles.cjs':{
   load:async()=>({selected:'pc-switch1',profiles:{}}),save:async()=>{},
   resolve:()=>({id:'pc-switch1',inputAdapter:'switch-wifi',setupRequired:true,settings:{},env:{}}),
   createInputLink:async()=>({}),createChecks:async()=>({})},
  '../hardware/devices/switch1/pairing.cjs':{restore:async()=>{pairingChecks++;return {profile:{},discovery:{paired:false}};}},
  '../core/session/connections.cjs':{createConnections:async()=>({})},
  '/fake/civ7/adapter.cjs':{createSession:()=>{
   sessions++;let phase='idle';
   current={snapshot:async()=>({active:null}),ownsListener:()=>false,
    action:async()=>{phase='install-done';return {ok:true};},refresh:async()=>({phase})};
   return current;
  }},
 };
 const exported={exports:{}};
 const wrapper=vm.runInThisContext('(function(require,module,exports){'+fs.readFileSync(file,'utf8')+'\n})',{filename:file});
 wrapper(name=>Object.hasOwn(mocks,name)?mocks[name]:realRequire(name),exported,exported.exports);
 const manager=await exported.exports.create({root:'/fake',settings:{},node:process.execPath});
 await manager.iz2scivAction({action:'install'});
 for(let i=0;i<3;i++)assert.equal((await manager.refresh(true)).phase,'install-done');
 assert.equal(sessions,1);assert.equal(pairingChecks,1);
});
