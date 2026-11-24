'use strict';
const test=require('node:test'),assert=require('node:assert/strict'),path=require('node:path');
const {engineAvailable}=require('../core/packages/store-catalog.cjs');
const civ=require('../engines/IzCivSwitchFS/engine.json');
const shell=require('../shells/izlauncher/shell.json');
for(const host of ['linux-x64','linux-arm64'])test(host+': Civ7 panel and console shell are available',()=>{
 assert.equal(engineAvailable({...civ,supported:civ.platforms.includes(host)},'original'),true);
 assert(shell.platforms.includes(host));
});
test('host support does not bypass incompatible engines or modes',()=>{
 assert.equal(engineAvailable({...civ,supported:false},'original'),false);
 assert.equal(engineAvailable({...civ,supported:true},'izlauncher'),false);
});
test('discovery exposes only the Civ7 entry and its direct PC profile',()=>{
 const root=path.resolve(__dirname,'..'),D=require('../core/descriptors.cjs');
 const engines=D.scan(root,'engine'),profiles=D.scan(root,'profile'),shells=D.scan(root,'shell');
 assert.deepEqual(engines.errors,[]);assert.deepEqual(profiles.errors,[]);assert.deepEqual(shells.errors,[]);
 assert.deepEqual(engines.items.map(e=>e.id),['iz2sciv-core']);
 assert.deepEqual(profiles.items.map(e=>e.id),['pc-civ7']);
 assert.deepEqual(shells.items.map(e=>e.id),['izlauncher']);
 const resolved=require('../hardware/configurations/pc-civ7/entry.cjs').resolve({settings:{consolePort:1234}});
 assert.equal(resolved.settings.consolePort,9026);
});
