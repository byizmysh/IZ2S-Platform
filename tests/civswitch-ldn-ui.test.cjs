'use strict';
const {test} = require('node:test'), assert = require('node:assert/strict');
const fs = require('node:fs'), path = require('node:path'), vm = require('node:vm');
const html = fs.readFileSync(path.join(__dirname, '../panel/ui/panel.html'), 'utf8');
const source = fs.readFileSync(path.join(__dirname, '../panel/ui/civswitch.js'), 'utf8');
function fixture() {
  const elements = new Map([...html.matchAll(/\bid="([^"]+)"/g)].map(match => [match[1], {
    value: '', checked: false, hidden: false, disabled: false, textContent: '',
    classList: {toggle() {}}, setAttribute() {}, replaceChildren() {}, append() {}, insertBefore() {}, querySelector() {return null;},
  }]));
  const $ = id => elements.get(id) || null;
  const document = {getElementById: $, createElement: () => ({}), activeElement: null};
  const requests = [], context = {document}; vm.runInNewContext(source, context);
  const ui = context.IZ2S_CIVSWITCH_LDN.create({document, act: async (route, body) => {requests.push({route, body}); return {ok: true};}});
  const state = {platform: {loaderId: 'civswitch-ldn', variant: 'original'}, civswitchLdn: {
    phase: 'idle', total: 19, index: 0, suggestedHost: '10.42.0.20', phy: 'phy0',
    chatDelay: 10, handshakeScale: 1, prepared: false,
    setup: {supported: true, radios: ['phy0'], dependenciesReady: true, keysReady: false, capabilitiesReady: true, networkManager: true}}};
  return {$, ui, state, requests};
}
test('the actual page initializes the LAN wizard and requires prod.keys before continuing or sending', () => {
  const {$, ui, state} = fixture(); ui.render(state);
  assert.equal($('civswitch-ldn-flow').hidden, false);
  assert.match($('ldn-keys-status').textContent, /Required.*prod.keys/);
  assert.equal($('ldn-next-1').disabled, true);
  $('ldn-prepared').checked = true; ui.render(state);
  assert.equal($('ldn-start').disabled, true);
  state.civswitchLdn.setup.keysReady = true; state.civswitchLdn.prepared = true; ui.render(state);
  assert.equal($('ldn-next-1').disabled, false);
  assert.equal($('ldn-start').disabled, false);
});
test('the LAN wizard sends one network action, with save consent enforced and reset after acceptance', async () => {
  const {$, ui, state, requests} = fixture(); state.civswitchLdn.prepared = true;
  $('ldn-prepared').checked = true; $('ldn-save-after').checked = true; ui.render(state);
  assert.equal($('ldn-start').disabled, true);
  $('ldn-delete-saves').checked = true; ui.render(state);
  assert.equal($('ldn-start').disabled, false);
  await $('ldn-start').onclick();
  assert.equal(requests.length, 1); assert.equal(requests[0].route, '/api/civswitch-ldn');
  assert.equal(requests[0].body.action, 'start'); assert.equal(requests[0].body.installAfterSuccess, true);
  assert.equal(requests[0].body.deleteSingleSaves, true);
  assert.equal($('ldn-prepared').checked, false); assert.equal($('ldn-delete-saves').checked, false);
});
test('key file selection imports only into the local action and clears the file control', async () => {
  const {$, ui, state, requests} = fixture(); ui.render(state);
  $('ldn-keys-file').value = 'prod.keys';
  $('ldn-keys-file').files = [{size: 10, text: async () => 'synthetic test file'}];
  await $('ldn-keys-file').onchange();
  assert.equal(requests[0].body.action, 'import-keys');
  assert.equal(requests[0].body.content, 'synthetic test file');
  assert.equal($('ldn-keys-file').value, '');
  $('ldn-keys-file').files = [{size: 65537, text: async () => {throw Error('Oversized file was read');}}];
  await $('ldn-keys-file').onchange(); assert.equal(requests.length, 1);
});
test('Continue performs the permission refresh without a second Check preparation action',async()=>{
 const {$,ui,state,requests}=fixture();state.civswitchLdn.setup.keysReady=true;state.civswitchLdn.setup.capabilitiesReady=false;
 ui.render(state);assert.equal($('ldn-next-1').disabled,false);
 await $('ldn-next-1').onclick();assert.equal(requests[0].body.action,'refresh');
});
test('both game branches retain large buttons and LAN uses the same wizard steps with a red stop action', () => {
  assert(html.includes('id="game-sm4"') && html.includes('id="game-civ7"'));
  assert(html.includes('id="civ-mode-usb"') && html.includes('id="civ-mode-ldn"'));
  assert(html.includes('class="wizard-steps civ-steps" aria-label="Civilization VII Wi-Fi setup steps"'));
  assert(html.includes('id="ldn-stop" class="danger"'));
  assert(html.includes('Opening CHAT and pressing A are not required.'));
  const ids = [...html.matchAll(/\bid="([^"]+)"/g)].map(match => match[1]);
  assert.equal(ids.length, new Set(ids).size);
});

test('an imported key remains visibly stored after the file selection is cleared',()=>{
 const {$,ui,state}=fixture();state.civswitchLdn.setup.keysReady=true;ui.render(state);
 assert.equal($('ldn-keys-field').hidden,true);assert.equal($('ldn-keys-stored').hidden,false);assert.equal($('ldn-keys-replace').disabled,false);
 assert.match($('ldn-keys-status').textContent,/added/);
 state.civswitchLdn.busy=true;ui.render(state);assert.equal($('ldn-keys-replace').disabled,true);
});

test('the connection step advances with an empty optional IP and keeps a manual loader check available',async()=>{
 const {$,ui,state,requests}=fixture();state.civswitchLdn.prepared=true;state.civswitchLdn.suggestedHost='';ui.render(state);
 assert.equal($('ldn-next-2').disabled,false);assert.equal($('ldn-use-save').disabled,false);
 await $('ldn-next-2').onclick();assert.equal(requests[0].body.host,'');
});
test('Launch LAN uses its console address and only verifies TCP without repeating LDN delivery',async()=>{
 const {$,ui,state,requests}=fixture();state.civswitchLdn.suggestedHost='192.168.1.122';
 state.connections={devices:{lan:{lastKnownAddresses:{lan:{address:'10.42.0.20'}}}}};
 ui.render(state);$('ldn-view-run').onclick();$('ldn-run-lan').onclick();
 assert.equal($('ldn-run-host').value,'10.42.0.20');
 await $('ldn-check-loader').onclick();
 assert.equal(requests.length,1);assert.equal(requests[0].body.action,'check-loader');
 assert.equal(requests[0].body.networkMode,'lan');assert.equal(requests[0].body.host,'10.42.0.20');assert.equal(requests[0].body.automatic,true);
 $('ldn-run-wifi').onclick();assert.equal($('ldn-run-host').value,'192.168.1.122');
});

test('saved-loader shortcut only changes view; manual IP remains an explicit alternative',async()=>{
 const {$,ui,state,requests}=fixture();state.civswitchLdn.setup.dependenciesReady=false;ui.render(state);
 await $('ldn-use-save').onclick();assert.equal(requests.length,0);assert.equal($('ldn-run-connection').hidden,false);
 assert.equal($('ldn-check-loader').disabled,false);
 $('ldn-run-host').value='10.42.0.21';await $('ldn-connect-ip').onclick();
 assert.equal(requests[0].body.host,'10.42.0.21');assert.equal(requests[0].body.automatic,false);
});

test('changing transport cannot launch through the previously verified endpoint',()=>{
 const {$,ui,state}=fixture();state.civswitchLdn.loaderReady=true;state.civswitchLdn.networkMode='lan';
 state.connections={devices:{lan:{lastKnownAddresses:{lan:{address:'10.42.0.20'},wifi:{address:'192.168.1.122'}}}}};
 ui.render(state);$('ldn-view-run').onclick();assert.equal($('ldn-launch').disabled,false);
 $('ldn-run-wifi').onclick();assert.equal($('ldn-launch').disabled,true);assert.equal($('ldn-check-loader').hidden,false);
});
test('a manually edited run address survives status rendering and is sent without discovery',async()=>{
 const {$,ui,state,requests}=fixture();state.civswitchLdn.loaderReady=true;state.civswitchLdn.networkMode='wifi';ui.render(state);$('ldn-view-run').onclick();
 $('ldn-run-host').value='192.168.1.123';ui.render(state);assert.equal($('ldn-run-host').value,'192.168.1.123');
 await $('ldn-connect-ip').onclick();assert.equal(requests[0].body.host,'192.168.1.123');assert.equal(requests[0].body.automatic,false);
});

test('Core only never offers an LDN launcher start action, and returning to a launcher restores it',()=>{
 const {$,ui,state}=fixture();state.civLauncher={shell:'original',state:'idle'};ui.render(state);$('ldn-view-run').onclick();
 assert.equal($('ldn-launch').hidden,true);
 state.civLauncher.shell='izlauncher';ui.render(state);assert.equal($('ldn-launch').hidden,false);
});

test('First Setup starts from preparation after an old LDN completion and does not jump back on polling',()=>{
 const {$,ui,state}=fixture();state.civswitchLdn.phase='entered';state.civswitchLdn.index=19;ui.render(state);
 assert.equal($('ldn-stage-1').hidden,false);assert.equal($('ldn-progress').hidden,true);
 $('ldn-view-run').onclick();$('ldn-view-setup').onclick();ui.render(state);ui.render(state);
 assert.equal($('ldn-stage-1').hidden,false);assert.equal($('ldn-stage-4').hidden,true);
 state.civswitchLdn.busy=true;state.civswitchLdn.phase='sending';ui.render(state);assert.equal($('ldn-stage-3').hidden,false);
 state.civswitchLdn.phase='entered';state.civswitchLdn.busy=false;ui.render(state);assert.equal($('ldn-stage-4').hidden,false);
 $('ldn-view-setup').onclick();assert.equal($('ldn-stage-1').hidden,false);
});

test('Wi-Fi permissions remain visible after setup and copying never starts a network action',async()=>{
 const {$,ui,state,requests}=fixture();
 const command="sudo setcap cap_net_admin,cap_net_raw=ep '/opt/IZ2S panel/data/runtime/civswitchldn/bin/python-ldn'";
 state.civswitchLdn.setup.capabilityCommand=command;ui.render(state);
 assert.equal($('ldn-permissions').hidden,false);assert.equal($('ldn-cap-command').value,command);assert.equal($('ldn-copy-cap').disabled,false);
 assert.match($('ldn-permissions-note').textContent,/Permissions are enabled/);
 await $('ldn-copy-cap').onclick();assert.equal(requests.length,0);assert.match($('ldn-copy-status').textContent,/Ctrl\+C|copied/);
 state.civswitchLdn.setup.capabilitiesReady=false;ui.render(state);assert.equal($('ldn-permissions').hidden,false);assert.match($('ldn-permissions-note').textContent,/terminal/);
 state.civswitchLdn.setup.supported=false;ui.render(state);assert.equal($('ldn-permissions').hidden,true);
});

test('restoration failure returns to preparation once; a restored connection offers explicit retry on step 3',()=>{
 const {$,ui,state}=fixture();state.civswitchLdn.prepared=true;ui.render(state);
 state.civswitchLdn.phase='sending';state.civswitchLdn.busy=true;state.civswitchLdn.id='new-attempt';ui.render(state);assert.equal($('ldn-stage-3').hidden,false);
 $('ldn-prepared').checked=true;state.civswitchLdn.phase='interrupted';state.civswitchLdn.busy=false;state.civswitchLdn.retryStep=1;ui.render(state);
 assert.equal($('ldn-stage-1').hidden,false);assert.equal($('ldn-prepared').checked,false);
 $('ldn-step-2').onclick();ui.render(state);assert.equal($('ldn-stage-2').hidden,false,'polling must not keep forcing the first step');
 state.civswitchLdn.id='retry';state.civswitchLdn.phase='sending';state.civswitchLdn.busy=true;ui.render(state);
 state.civswitchLdn.phase='interrupted';state.civswitchLdn.busy=false;state.civswitchLdn.retryStep=3;ui.render(state);
 assert.equal($('ldn-stage-3').hidden,false);assert.equal($('ldn-start').textContent,'Retry delivery');assert.equal($('ldn-start').disabled,true);
 $('ldn-prepared').checked=true;ui.render(state);assert.equal($('ldn-start').disabled,false);
});

test('recovery navigation works with a running older server that has no retryStep field',()=>{
 const {$,ui,state}=fixture();ui.render(state);state.civswitchLdn.id='old-server';state.civswitchLdn.phase='sending';state.civswitchLdn.busy=true;ui.render(state);
 Object.assign(state.civswitchLdn,{busy:false,phase:'entered',index:19,networkRestored:false,error:'Wi-Fi restoration failed'});ui.render(state);
 assert.equal($('ldn-stage-1').hidden,false);assert.equal($('ldn-stage-4').hidden,true);
});

test('LDN Launch requests automatic startup for the selected shell; Core only remains a probe',async()=>{
 const {$,ui,state,requests}=fixture();state.civLauncher={shell:'izpslauncher',state:'idle'};ui.render(state);$('ldn-view-run').onclick();
 await $('ldn-check-loader').onclick();assert.equal(requests[0].body.launchAfterSuccess,true);assert.equal(requests[0].body.launcher,'izpslauncher');
 state.civLauncher.shell='original';ui.render(state);await $('ldn-check-loader').onclick();assert.equal(requests[1].body.launchAfterSuccess,false);
});
test('LDN save review obtains fresh consent and does not send another delivery',async()=>{
 const {$,ui,state,requests}=fixture();Object.assign(state.civswitchLdn,{phase:'ready',loaderReady:true,prepared:true});state.civswitchLdn.setup.keysReady=true;ui.render(state);$('ldn-step-4').onclick();
 assert.match($('ldn-save-status').textContent,/not confirmed/);await $('ldn-install').onclick();assert.equal(requests.length,0);assert.equal($('ldn-stage-1').hidden,false);
 $('ldn-delete-saves').checked=true;ui.render(state);await $('ldn-next-1').onclick();assert.equal(requests[0].body.action,'install');assert.equal(requests[0].body.deleteSingleSaves,true);
 state.civswitchLdn.phase='installed';ui.render(state);assert.match($('ldn-save-status').textContent,/Save installed.*Load Game.*Launch/);assert.equal($('ldn-install').hidden,true);
});

test('Civ7 offline and shutdown states show their real status and never hide the error as preparation',async()=>{
 const {$,ui,state}=await fixture();state.civLauncher={shell:'izlauncher',state:'offline',error:'PC connection was lost.'};ui.render(state);
 assert.equal($('ldn-status').textContent,'Connection interrupted');assert.equal($('ldn-launcher-status').hidden,false);assert.equal($('ldn-launcher-status').textContent,'PC connection was lost.');assert.equal($('ldn-stop').disabled,false);
 state.civLauncher={shell:'izlauncher',state:'stopping',shutdownUnconfirmed:true,error:'Shutdown unconfirmed'};ui.render(state);
 assert.equal($('ldn-status').textContent,'Shutdown unconfirmed');assert.equal($('ldn-launcher-status').hidden,false);assert.equal($('ldn-stop').textContent,'Shutdown unconfirmed');assert.equal($('ldn-stop').disabled,true);
 state.civLauncher={shell:'izlauncher',state:'starting'};ui.render(state);assert.equal($('ldn-status').textContent,'Preparing the Civ7 launcher.');
});

test('Launch PC readiness is independent of LDN setup and restores setup status when going back',()=>{
 const {$,ui,state}=fixture(),classes=new Set(),labels={'.state':{textContent:''},'.device-detail':{textContent:''}};
 const card={classList:{toggle(name,on){if(on)classes.add(name);else classes.delete(name);}},querySelector:s=>labels[s]||null};
 // The server card is dynamically rendered, so attach it to this DOM fixture.
 const htmlNodes=new Map([...html.matchAll(/\bid="([^"]+)"/g)].map(match=>[match[1],$(match[1])]));
 const document={getElementById:id=>id==='device-server'?card:htmlNodes.get(id)||null,createElement:()=>({}),activeElement:null};
 const context={document};vm.runInNewContext(source,context);
 const view=context.IZ2S_CIVSWITCH_LDN.create({document,act:async()=>{throw Error('A view change must not contact the console');}});
 state.checks={items:[{id:'server',ready:false,pending:false,detail:'Complete PC setup and add prod.keys in step 1.'}]};
 state.civswitchLdn.setup.radios=['phy7'];state.civswitchLdn.phy='phy0';state.civswitchLdn.loaderReady=false;
 view.render(state);$('ldn-view-setup').onclick();assert.equal(classes.has('ready'),false);
 $('ldn-view-run').onclick();assert.equal(classes.has('ready'),true);assert.equal(classes.has('pending'),false);
 assert.equal(labels['.state'].textContent,'✓ READY');assert.equal(state.civswitchLdn.loaderReady,false);
 assert.equal($('ldn-launch').disabled,true);
 $('ldn-view-setup').onclick();assert.equal(classes.has('ready'),false);assert.equal(labels['.state'].textContent,'○ WAITING');
 assert.equal(labels['.device-detail'].textContent,state.checks.items[0].detail);
});

test('the visible PC LAN/Wi-Fi picker changes the existing console transport and uses it for connection',async()=>{
 const {$,ui,state,requests}=fixture();state.connections={devices:{lan:{addresses:{lan:'10.42.0.20',wifi:'192.168.1.22'}}}};
 state.civswitchLdn.networkMode='wifi';ui.render(state);$('ldn-view-run').onclick();
 assert.equal($('ldn-run-source').hidden,false);
 $('ldn-source-lan').onclick();assert.equal($('ldn-run-host').value,'10.42.0.20');
 await $('ldn-connect-ip').onclick();assert.equal(requests.at(-1).body.networkMode,'lan');assert.equal(requests.at(-1).body.host,'10.42.0.20');
 $('ldn-source-wifi').onclick();assert.equal($('ldn-run-host').value,'192.168.1.22');
 await $('ldn-connect-ip').onclick();assert.equal(requests.at(-1).body.networkMode,'wifi');
 $('ldn-view-setup').onclick();assert.equal($('ldn-run-source').hidden,true);
 state.civswitchLdn.busy=true;ui.render(state);$('ldn-view-run').onclick();assert.equal($('ldn-source-lan').disabled,true);assert.equal($('ldn-source-wifi').disabled,true);
});
