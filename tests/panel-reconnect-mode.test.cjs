'use strict';
const test=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),vm=require('node:vm'),ui=require('../panel/ui/presentation.js');
const active={id:'same-session',kind:'launch',status:'offline'},link={protocol:3,recoveryAvailable:true,state:'waiting-for-network'};
test('restoration is offered only for a retained launch with the native recovery service',()=>{
 assert.equal(ui.connectionMode({active,link}).preferred,'restore');assert.equal(ui.connectionMode({active,link}).canRestore,true);
 for(const state of [{},{link},{active},{active:{...active,status:'starting'},link},{active:{...active,kind:'bootstrap'},link},{active,link:{...link,protocol:1}},{active,link:{...link,recoveryAvailable:false}}])assert.equal(ui.connectionMode(state).canRestore,false);
});
test('requesting a wake is not shown as a restored connection until the launcher session is connected',()=>{
 for(const state of ['synchronizing','waiting-for-command-completion']){const s=ui.connectionMode({active,link:{...link,state}});assert.equal(s.canRestore,false);assert.equal(s.online,false);assert.equal(s.label,'Synchronizing session…');}
 const connected=ui.connectionMode({active:{...active,status:'running'},connected:true,link:{...link,state:'connected'}});assert.equal(connected.online,true);assert.equal(connected.canRestore,false);assert.equal(connected.label,'Connection restored');assert.equal(ui.connectionMode({active,connected:true,link:{...link,state:'connected'}}).online,false);
});
test('the actual mode renderer hides fresh input and preloading while restoring the current launcher',()=>{
 const source=fs.readFileSync(path.join(__dirname,'../panel/ui/app.js'),'utf8'),begin=source.indexOf('let sessionModeChoice='),end=source.indexOf('function renderControls()',begin),nodes=new Map(),$=id=>{if(!nodes.has(id))nodes.set(id,{hidden:false,disabled:false,classList:{toggle(){}},setAttribute(){}});return nodes.get(id);};
 const context={globalThis:{IZ2S_PRESENTATION:ui},state:{active,link,connected:false,platform:{loaderId:'izmysh',izModules:true}},$,step:'launch',busy:false,serverUnavailable:false};vm.createContext(context);const render=vm.runInContext(source.slice(begin,end)+'\nrenderSessionConnectionMode;',context);render();
 assert.equal($('session-mode').hidden,false);assert.equal($('session-mode-new').disabled,true);assert.equal($('session-mode-restore').disabled,false);assert.equal($('restore-connection').hidden,false);assert.equal($('restore-connection').disabled,false);for(const id of ['start','session-confirmation','launch-modules','delivery-options'])assert.equal($(id).hidden,true,id);
 context.state.connected=true;context.state.active={...active,status:'running'};context.state.link={...link,state:'connected'};render();assert.equal($('restore-connection').disabled,true);assert.equal($('restore-connection').textContent,'Connection restored');
 context.serverUnavailable=true;render();assert.equal($('session-mode-restore').disabled,true);
 context.state.platform={loaderId:'civswitch',izModules:false};render();assert.equal($('session-mode').hidden,true);
});
test('both mode choices and the existing restore action are included in the shipped page and bundle',()=>{
 const script=fs.readFileSync(path.join(__dirname,'../panel/ui/app.js'),'utf8');for(const file of ['panel.html','index.html']){const html=fs.readFileSync(path.join(__dirname,'../panel/ui',file),'utf8');for(const id of ['session-mode','session-mode-new','session-mode-restore','session-mode-note','restore-connection'])assert(html.includes('id="'+id+'"'),file+': '+id);}assert(script.includes("$('restore-connection').addEventListener('click',()=>act('/api/connection/restore',{}))"));assert(script.includes('function connectionMode(state)'));
});
