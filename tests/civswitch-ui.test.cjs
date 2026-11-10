'use strict';
const {test}=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs/promises'),path=require('node:path'),vm=require('node:vm');
async function fixture({duringAction}={}){
 const elements=new Map();const $=id=>{if(!elements.has(id))elements.set(id,{value:'',checked:false,textContent:'',hidden:false,disabled:false,classList:{toggle(){}},setAttribute(){},insertBefore(){},append(){},querySelector(){return null;}});return elements.get(id);};
 const document={getElementById:$,activeElement:null};const context={document};
 vm.runInNewContext(await fs.readFile(path.join(__dirname,'../panel/ui/civswitch.js'),'utf8'),context);
 const requests=[];const ui=context.IZ2S_CIVSWITCH.create({document,act:async(url,body)=>{requests.push({url,body});duringAction?.(ui,state);return {ok:true,started:true};}});
 const state={platform:{loaderId:'civswitch',variant:'original',configuration:{inputAdapter:'cardputer-wifi'}},civswitch:{phase:'chat',index:0,total:87,busy:false,loaderReady:false,suggestedHost:'10.42.0.20'}};
 return {$,ui,state,document,requests};
}
test('installation explains its loader prerequisite; deletion acknowledgement alone cannot enable it',async()=>{
 const {$,ui,state}=await fixture();$('civ-save-after').checked=true;$('civ-delete-saves').checked=true;ui.render(state);
 assert.equal($('civ-install').disabled,true);assert.match($('civ-install-hint').textContent,/verified game loader/);
 state.civswitch.loaderReady=true;ui.render(state);assert.equal($('civ-install').disabled,false);
 $('civ-delete-saves').checked=false;ui.render(state);assert.equal($('civ-install').disabled,false);assert.equal($('civ-install').textContent,'Review save installation');assert.match($('civ-install-hint').textContent,/Confirm save deletion/);
 state.civswitch.busy=true;ui.render(state);assert.match($('civ-install-hint').textContent,/current operation/);
});
test('automatic input requires upfront save consent and works identically for ADV and ARM',async()=>{
 const {$,ui,state,requests}=await fixture();state.civswitch.automaticInputAvailable=true;
 $('civ-prepared').checked=true;$('civ-save-after').checked=true;ui.render(state);
 assert.equal($('civ-auto-chat').disabled,true);
 assert.equal($('civ-save-consent').hidden,false);assert.equal($('civ-next-1').disabled,true);
 $('civ-delete-saves').checked=true;ui.render(state);assert.equal($('civ-auto-chat').disabled,false);
 await $('civ-auto-chat').onclick();assert.equal(requests.length,1);
 assert.equal(requests[0].body.action,'auto-chat');assert.equal(requests[0].body.installAfterSuccess,true);
 assert.equal(requests[0].body.deleteSingleSaves,true);assert.equal($('civ-delete-saves').checked,false);
 state.platform.configuration.inputAdapter='ssh-gadget';ui.render(state);
 assert.equal($('civ-auto-chat').hidden,false);assert.equal($('civ-auto-chat').disabled,true);
});
test('a fast automatic completion stays on Result instead of returning to the input screen',async()=>{
 const {$,ui,state}=await fixture({duringAction:(ui,state)=>{state.civswitch.phase='ready';state.civswitch.loaderReady=true;state.civswitch.index=87;ui.render(state);}});
 state.civswitch.automaticInputAvailable=true;$('civ-prepared').checked=true;ui.render(state);
 await $('civ-auto-chat').onclick();
 assert.equal($('civ-stage-4').hidden,false);assert.equal($('civ-stage-3').hidden,true);assert.equal($('civ-prepared').checked,false);
});
test('the Civ host follows console address changes without overwriting an address being edited',async()=>{
 const {$,ui,state,document}=await fixture();ui.render(state);assert.equal($('civ-host').value,'10.42.0.20');
 state.civswitch.suggestedHost='10.42.0.172';ui.render(state);assert.equal($('civ-host').value,'10.42.0.172');
 document.activeElement=$('civ-host');$('civ-host').value='10.42.0.21';state.civswitch.suggestedHost='10.42.0.22';ui.render(state);
 assert.equal($('civ-host').value,'10.42.0.21');
});

test('the shipped page includes both game branches and every CivSwitch wizard control',async()=>{
 const html=await fs.readFile(path.join(__dirname,'../panel/ui/panel.html'),'utf8');
 const source=await fs.readFile(path.join(__dirname,'../panel/ui/civswitch.js'),'utf8');
 const ids=[...html.matchAll(/\bid="([^"]+)"/g)].map(match=>match[1]);
 const required=[...source.matchAll(/\$\('(civ-[a-z-]+)'\)/g)].map(match=>match[1]);
 for(let i=1;i<=4;i++)required.push('civ-stage-'+i,'civ-step-'+i);
 assert(html.includes('https://www.reddit.com/r/switch2hacks/comments/1xll4wf/civswitch_hack_start_to_finish/'));
 assert(!html.includes('id="civ-chat-ready"'));
 assert(!html.includes('id="sd-launcher-enabled"'));
 assert(html.includes('class="wizard-steps civ-steps"'));
 assert(html.indexOf('id="library-source-options"')<html.indexOf('id="apps"'));
 assert(!html.includes('/menus.css'));
 for(const id of ['game-sm4','game-civ7','civ-video','library-source','library-source-options','library-source-note',...required])assert.equal(ids.filter(value=>value===id).length,1,id);
});
test('CivSwitch initializes against the shipped HTML without inventing missing controls',async()=>{
 const html=await fs.readFile(path.join(__dirname,'../panel/ui/panel.html'),'utf8');
 const elements=new Map([...html.matchAll(/\bid="([^"]+)"/g)].map(match=>[match[1],{}]));
 const context={document:{getElementById:id=>elements.get(id)||null}};
 vm.runInNewContext(await fs.readFile(path.join(__dirname,'../panel/ui/civswitch.js'),'utf8'),context);
 assert.doesNotThrow(()=>context.IZ2S_CIVSWITCH.create({document:context.document,act(){}}));
});

test('historical input errors are separate from current status and can be reset without a lobby acknowledgement',async()=>{
 const {$,ui,state,requests}=await fixture();state.civswitch={...state.civswitch,phase:'uncertain',index:33,restoredAttempt:true,error:'The operation was aborted due to timeout'};
 ui.render(state);assert.equal($('civ-error').hidden,true);assert.equal($('civ-status').hidden,true);assert.equal($('civ-previous').hidden,false);
 await $('civ-discard').onclick();assert.equal(requests[0].body.action,'discard');assert.equal(requests[0].body.confirm,false);
 state.civswitch.restoredAttempt=false;ui.render(state);assert.equal($('civ-error').hidden,false);assert.equal($('civ-previous').hidden,true);
});

test('Core only never offers a Civ USB launcher start action, and returning to a launcher restores it',async()=>{
 const {$,ui,state}=await fixture();state.civLauncher={shell:'original',state:'idle'};ui.render(state);$('civ-view-run').onclick();
 assert.equal($('civ-launch').hidden,true);
 state.civLauncher.shell='izpslauncher';ui.render(state);assert.equal($('civ-launch').hidden,false);
});

test('First Setup opens at step one after a completed USB attempt and stays there across polling',async()=>{
 const {$,ui,state}=await fixture();state.civswitch.phase='entered';state.civswitch.index=87;ui.render(state);
 assert.equal($('civ-stage-1').hidden,false);assert.equal($('civ-progress').hidden,true);
 $('civ-view-run').onclick();$('civ-view-setup').onclick();ui.render(state);ui.render(state);
 assert.equal($('civ-stage-1').hidden,false);assert.equal($('civ-stage-4').hidden,true);
 state.civswitch.busy=true;state.civswitch.phase='auto-typing';ui.render(state);assert.equal($('civ-stage-3').hidden,false);
 state.civswitch.phase='ready';state.civswitch.busy=false;ui.render(state);assert.equal($('civ-stage-4').hidden,false);
 $('civ-view-setup').onclick();assert.equal($('civ-stage-1').hidden,false);
});
test('a completed USB attempt starts a fresh sequence only after a new lobby acknowledgement',async()=>{
 const {$,ui,state,requests}=await fixture();state.civswitch.phase='entered';state.civswitch.index=87;state.civswitch.automaticInputAvailable=true;ui.render(state);
 assert.equal($('civ-auto-chat').disabled,true);$('civ-prepared').checked=true;ui.render(state);assert.equal($('civ-auto-chat').disabled,false);
 await $('civ-auto-chat').onclick();assert.deepEqual(requests.map(r=>r.body.action),['reset','auto-chat']);assert.equal(requests[0].body.confirm,true);
});
test('Civ USB exposes the ARM installer for ARM input and uses blue standby without claiming a connection',async()=>{
 const {$,ui,state}=await fixture(),classes=new Set(),labels={'.state':{textContent:''},'.device-detail':{textContent:''}};
 $('device-lan').classList={toggle:(name,on)=>on?classes.add(name):classes.delete(name)};$('device-lan').querySelector=selector=>labels[selector]||null;
 state.platform.configuration.inputAdapter='ssh-gadget';ui.render(state);
 assert.equal($('civ-arm-installer').hidden,false);assert.equal(classes.has('awaiting-loader'),true);assert.equal(classes.has('ready'),false);assert.equal(labels['.state'].textContent,'AWAITING LOADER');
 state.civswitch.error='Loader did not respond';ui.render(state);assert.equal(classes.has('awaiting-loader'),false);
 state.civswitch.error='';state.civswitch.loaderReady=true;ui.render(state);assert.equal(classes.has('ready'),true);assert.equal(classes.has('awaiting-loader'),false);
 state.platform.configuration.inputAdapter='cardputer-wifi';ui.render(state);assert.equal($('civ-arm-installer').hidden,true);
});

test('USB Launch connects and requests one automatic startup with the selected modules',async()=>{
 const {$,ui,state,requests}=await fixture();state.civLauncher={shell:'izpslauncher',state:'idle'};ui.render(state);$('civ-view-run').onclick();
 await $('civ-check-loader').onclick();assert.equal(requests[0].body.launchAfterSuccess,true);assert.equal(requests[0].body.launcher,'izpslauncher');
 state.civLauncher.shell='original';ui.render(state);await $('civ-check-loader').onclick();assert.equal(requests[1].body.launchAfterSuccess,false);
});
test('USB save review returns to consent without sending an install or repeating chat input',async()=>{
 const {$,ui,state,requests}=await fixture();state.civswitch.loaderReady=true;state.civswitch.phase='ready';ui.render(state);$('civ-step-4').onclick();
 await $('civ-install').onclick();assert.equal(requests.length,0);assert.equal($('civ-stage-1').hidden,false);assert.equal($('civ-save-after').checked,true);
 $('civ-delete-saves').checked=true;ui.render(state);assert.equal($('civ-next-1').textContent,'Install save');await $('civ-next-1').onclick();assert.equal(requests.length,1);assert.equal(requests[0].body.action,'install');assert.equal(requests[0].body.deleteSingleSaves,true);
});

test('Civ7 offline and shutdown states show their real status and never hide the error as preparation',async()=>{
 const {$,ui,state}=await fixture();state.civLauncher={shell:'izlauncher',state:'offline',error:'PC connection was lost.'};ui.render(state);
 assert.equal($('civ-status').textContent,'Connection interrupted');assert.equal($('civ-launcher-status').hidden,false);assert.equal($('civ-launcher-status').textContent,'PC connection was lost.');assert.equal($('civ-stop').disabled,false);
 state.civLauncher={shell:'izlauncher',state:'stopping',shutdownUnconfirmed:true,error:'Shutdown unconfirmed'};ui.render(state);
 assert.equal($('civ-status').textContent,'Shutdown unconfirmed');assert.equal($('civ-launcher-status').hidden,false);assert.equal($('civ-stop').textContent,'Shutdown unconfirmed');assert.equal($('civ-stop').disabled,true);
 state.civLauncher={shell:'izlauncher',state:'starting'};ui.render(state);assert.equal($('civ-status').textContent,'Preparing the Civ7 launcher.');
});
