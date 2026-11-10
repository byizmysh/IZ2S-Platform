'use strict';
const {test}=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),vm=require('node:vm');
const source=fs.readFileSync(path.join(__dirname,'../panel/ui/app.js'),'utf8');
function fixture(loaderId){
 const nodes=new Map();
 function node(id){if(!nodes.has(id))nodes.set(id,{id,hidden:false,parentElement:null,append(child){child.parentElement=this;},querySelector(selector){return node(id+':'+selector);}});return nodes.get(id);}
 const context={$:node,serverUnavailable:false,busy:false,state:{platform:{loaderId,izModules:!loaderId.startsWith('civswitch'),configuration:{inputAdapter:'cardputer-wifi'}},checks:{items:[{id:'arm'}]}},document:{querySelector:()=>node('apps-nav')}};
 const render=vm.runInNewContext(source.slice(source.indexOf('function renderMenuLayout()'),source.indexOf('\nlet platformMenuStamp'))+'\nrenderMenuLayout;',context);
 return {render,nodes,node,context};
}
test('Civ USB keeps its device dashboard, settings and app library, without unrelated file actions',()=>{
 const f=fixture('civswitch');f.render();
 for(const id of ['devices','device-toolbar','hardware-options','apps-nav'])assert.equal(f.node(id).hidden,false,id);
 for(const id of ['network-caption','iz-flow','installation-options','connection-files','mode'])assert.equal(f.node(id).hidden,true,id);
});
test('LDN shows its required device tiles and hides ARM/USB setup',()=>{
 const f=fixture('civswitch-ldn');f.render();
 assert.equal(f.node('devices').hidden,false);assert.equal(f.node('hardware-options').hidden,true);assert.equal(f.node('adv-usb-next').hidden,true);
});
test('switching back to SmileBASIC restores the app library, file controls and hardware settings',()=>{
 const f=fixture('civswitch');f.render();
 f.context.state.platform.loaderId='izmysh';f.context.state.platform.izModules=true;f.render();
 for(const id of ['devices','device-toolbar','network-caption','apps-nav','hardware-options','connection-files','mode'])assert.equal(f.node(id).hidden,false,id);
});

test('an idle old server requests a restart without claiming a console session is active',()=>{
 const f=fixture('civswitch');f.render();
 assert.equal(f.node('civ-mode-ldn').disabled,true);
 assert.equal(f.node('civ-mode-ldn').title,'Restart the panel to enable CIV7 LAN.');
 f.context.state.platform.locked=true;f.render();
 assert.match(f.node('civ-mode-ldn').title,/ending the current session/);
 f.context.state.platform.locked=false;f.context.state.platform.civswitchLdnAvailable=true;f.render();
 assert.equal(f.node('civ-mode-ldn').disabled,false);assert.equal(f.node('civ-mode-ldn').title,'');
});

test('Civ console status does not ask for a SmileBASIC launcher while a wireless lobby is pending',()=>{
 const nodes=new Map(),node=id=>{if(!nodes.has(id))nodes.set(id,{hidden:false,textContent:''});return nodes.get(id);};
 const context={IZ2S_PRESENTATION:require('../panel/ui/presentation.js'),state:{platform:{loaderId:'civswitch'},connected:false},setText:(node,value)=>node.textContent=value};
 const render=vm.runInNewContext(source.slice(source.indexOf('function renderDeviceDetail('),source.indexOf("\ndocument.addEventListener('click'",source.indexOf('function renderDeviceDetail(')))+'\nrenderDeviceDetail;',context);
 render({querySelector:node},{id:'lan',pending:true,detail:'No LAN reply. Wireless lobby can disconnect normal networking.'});
 assert.equal(node('.detail-text').textContent,'Wireless lobby may pause LAN. The loader is checked after input.');
});

test('switching from ADV to LDN preserves the shared USB control before its tile is removed',()=>{
 const control={},calls=[],devices={contains:()=>true,after:node=>calls.push(node)};
 const context={$:id=>id==='devices'?devices:id==='adv-usb-next'?control:null,state:{checks:{items:[{id:'server'},{id:'lan'}]}}};
 const retain=vm.runInNewContext(source.slice(source.indexOf('function retainDeviceControls('),source.indexOf('function renderDevices()'))+'\nretainDeviceControls;',context);
 retain();assert.deepEqual(calls,[control]);calls.length=0;context.state.checks.items.push({id:'usb'});retain();assert.equal(calls.length,0);retain([{id:'server'},{id:'lan'}]);assert.deepEqual(calls,[control]);
});


test('changing games clears Civ-only console styling before rebuilding shared device controls',()=>{
 const classes=new Set(['device','civ-run-console']),card={classList:{remove:value=>classes.delete(value)}},devices={children:[card],contains:()=>false},control={};
 const context={$:id=>id==='devices'?devices:id==='adv-usb-next'?control:null,state:{checks:{items:[]}}};
 const retain=vm.runInNewContext(source.slice(source.indexOf('function retainDeviceControls('),source.indexOf('function renderDevices()'))+'\nretainDeviceControls;',context);
 retain();assert.equal(classes.has('civ-run-console'),false);assert.equal(classes.has('device'),true);
});

test('selecting Civilization defaults to Wi-Fi, with an explicit fallback for an older server',()=>{
 const button={},calls=[],state={platform:{loaderId:'izmysh',civswitchLdnAvailable:true}};
 const code=source.slice(source.indexOf("$('game-civ7').onclick="),source.indexOf("$('civ-mode-usb').onclick="));
 vm.runInNewContext(code,{$:()=>button,state,act:(route,body)=>calls.push({route,body})});
 button.onclick();assert.equal(calls.at(-1).body.loaderId,'civswitch-ldn');
 state.platform.loaderId='civswitch';button.onclick();assert.equal(calls.at(-1).body.loaderId,'civswitch-ldn');
 state.platform.civswitchLdnAvailable=false;button.onclick();assert.equal(calls.at(-1).body.loaderId,'civswitch');
});
