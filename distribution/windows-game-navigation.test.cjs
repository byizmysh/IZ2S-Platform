'use strict';
const test=require('node:test'),assert=require('node:assert/strict');
const path=require('node:path');
const {engineAvailable}=require('../core/packages/store-catalog.cjs');
const core=require('../engines/iz2s-core/engine.json');
const civ=require('../engines/IzCivSwitchFS/engine.json');
const shell=require('../shells/izlauncher/shell.json');
// Both game tabs must remain selectable on every supported installer host.
// Selection is checked independently of real console/network availability.
for(const host of ['win32-x64','linux-x64','linux-arm64']) {
 test(host+': both game tabs and the SmileBASIC shell are available',()=>{
  for(const [descriptor,variant] of [[core,'izlauncher'],[civ,'original']]) {
   const loader={...descriptor,supported:descriptor.platforms.includes(host)};
   assert.equal(engineAvailable(loader,variant),true,descriptor.id+' unavailable on '+host);
  }
  assert(shell.platforms.includes(host),'SmileBASIC shell unavailable on '+host);
 });
}
test('Windows support does not bypass incompatible engines or modes',()=>{
 assert.equal(engineAvailable({...core,supported:false},'izlauncher'),false);
 assert.equal(engineAvailable({...core,supported:true},'original'),false);
 assert.equal(engineAvailable({...civ,supported:true},'izlauncher'),false);
});
test('native descriptor discovery exposes both default game choices',()=>{
 if(!['linux-x64','linux-arm64','win32-x64'].includes(process.platform+'-'+process.arch))return;
 const root=path.resolve(__dirname,'..'),descriptors=require('../core/descriptors.cjs');
 const engines=descriptors.scan(root,'engine'),shells=descriptors.scan(root,'shell');
 assert.deepEqual(engines.errors,[]);assert.deepEqual(shells.errors,[]);
 for(const [id,variant]of [['iz2s-core','izlauncher'],['iz2sciv-core','original']])
  assert(engineAvailable(engines.items.find(e=>e.id===id),variant),id+' must be selectable');
 assert.equal(shells.items.find(s=>s.id==='izlauncher').supported,true);
});
