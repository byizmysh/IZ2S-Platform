'use strict';
const test=require('node:test'),assert=require('node:assert/strict'),vm=require('node:vm'),fs=require('node:fs'),path=require('node:path');
const source=fs.readFileSync(path.join(__dirname,'../panel/ui/app.js'),'utf8');
function fixture(reduced=false){
 const animations=[];
 const node=id=>({id,hidden:false,dataset:{},classList:{toggle(){}},style:{setProperty(){}},getAnimations:()=>[],getBoundingClientRect:()=>({x:0,y:0,width:300,height:180}),animate(frames){animations.push({id,frames});},contains:()=>false});
 const entries=Object.fromEntries(['devices','device-server','device-arm','device-usb','device-lan','recheck'].map(id=>[id,node(id)]));
 entries.devices.children=['server','arm','usb','lan'].map(id=>entries['device-'+id]);
 const context={renderDeviceConnections(){},state:{platform:{loaderId:'izmysh'}},inputDesign:'arm',$:id=>entries[id],document:{activeElement:null},window:{matchMedia:()=>({matches:reduced})},globalThis:{IZ2S_PRESENTATION:require('../panel/ui/presentation.js')}};
 const render=vm.runInNewContext(source.slice(source.indexOf('function renderDeviceTopology(){'),source.indexOf('function renderDevices() {'))+'\nrenderDeviceTopology;',context);
 return {entries,context,render,animations};
}
test('network handoff hides hardware, keeps its controls, and does not replay motion on status polling',()=>{
 const f=fixture();f.render();assert.equal(f.animations.length,0);
 f.context.state.connected=true;f.render();assert.equal(f.entries['device-arm'].hidden,true);assert.equal(f.entries['device-usb'].hidden,true);assert.equal(f.entries.devices.dataset.topology,'direct');
 const count=f.animations.length;assert(count>0);f.render();assert.equal(f.animations.length,count);
 f.context.state={platform:{loaderId:'izmysh'},active:{kind:'launch',status:'offline'}};f.render();assert.equal(f.entries['device-arm'].hidden,true);assert.equal(f.animations.length,count);
 f.context.state={platform:{loaderId:'izmysh'}};f.render();assert.equal(f.entries['device-arm'].hidden,false);assert.equal(f.entries['device-usb'].hidden,false);assert.equal(f.entries.devices.children.length,4);
});
test('reduced motion disables topology animation and Civ7 retains its input hardware',()=>{
 const f=fixture(true);f.render();f.context.state.connected=true;f.render();assert.equal(f.animations.length,0);
 f.context.state.platform.loaderId='civswitch';f.render();assert.equal(f.entries['device-arm'].hidden,false);assert.equal(f.entries['device-usb'].hidden,false);
});

test('connection paths follow all endpoints on desktop, wrapped rows and a single column',()=>{
 const {deviceLinks}=require('../panel/ui/presentation.js'),card=(x,y,tone='ready')=>({x,y,width:200,height:160,tone});
 const desktop=deviceLinks([card(0,0),card(240,0),card(480,0),card(720,0)]);assert.equal(desktop.length,3);assert.equal(desktop[0].x,220);assert.equal(desktop[0].y,80);assert(desktop.every(l=>l.tone==='ready'));
 const wrapped=deviceLinks([card(0,0),card(240,0),card(0,188),card(240,188)]);assert.equal(wrapped.length,3);assert.equal(wrapped[1].y,174);assert.match(wrapped[1].d,/ Q /);
 const mobile=deviceLinks([card(0,0),card(0,188,'waiting'),card(0,376,'lost')]);assert.equal(mobile[0].x,100);assert.equal(mobile[0].y,174);assert.equal(mobile[0].tone,'waiting');assert.equal(mobile[1].tone,'lost');
 assert.deepEqual(deviceLinks([]),[]);assert.deepEqual(deviceLinks([card(0,0)]),[]);
});

test('manual-input rendering cannot reveal USB inside the retained network layout during Restore connection',()=>{
 const f=fixture();
 const simple=hidden=>({hidden,classList:{toggle(){}}});
 for(const id of ['manual-input-design','manual-device-description','hardware-options'])f.entries[id]=simple(true);
 f.context.document.querySelector=()=>simple(false);
 f.context.state={platform:{loaderId:'izmysh',izModules:true},checks:{items:[]},connected:true,active:{kind:'launch',status:'running'}};
 f.render();const animations=f.animations.length;
 const manual=vm.runInNewContext(source.slice(source.indexOf('function renderManualInputDesign() {'),source.indexOf("\ndocument.querySelectorAll('[data-manual-stage]')"))+'\nrenderManualInputDesign;',f.context);
 for(const phase of [
  {connected:false,status:'offline',link:'waiting-for-network'},
  {connected:false,status:'running',link:'synchronizing'},
  {connected:false,status:'starting',launchStage:'initializing',link:'connected'},
  {connected:true,status:'running',link:'connected'},
 ]){
  f.context.state.connected=phase.connected;f.context.state.active={kind:'launch',status:phase.status,launchStage:phase.launchStage};f.context.state.link={state:phase.link,protocol:3,recoveryAvailable:true};
  manual();assert.equal(f.entries.devices.dataset.topology,'direct');assert.equal(f.entries['device-usb'].hidden,true);assert.equal(f.entries['device-arm'].hidden,true);assert.equal(f.entries.devices.children.filter(c=>!c.hidden).length,2);
 }
 assert.equal(f.animations.length,animations,'recovery status changes must not restart the topology animation');
});
