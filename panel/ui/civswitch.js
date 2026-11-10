'use strict';
globalThis.IZ2S_CIVSWITCH={create({document,act,setupInput=()=>{},downloadArm=()=>{},getStartup=()=>({}),isUnavailable=()=>false}){
 const $=id=>document.getElementById(id),t=value=>globalThis.RUN21_LANGUAGE?.t(value)||value;
 let current=null,snapshot=null,posting=false,stamp='',hostStamp='',step=1,wasShown=null,view='setup',viewChosen=false;
 [view,viewChosen]=civViewPreference('civ');
 const network=civRunNetwork(document,'civ',()=>render(snapshot));
 $('civ-run-host').oninput=()=>render(snapshot);
 for(const mode of ['setup','run'])$('civ-view-'+mode).onclick=()=>{view=mode;viewChosen=true;civViewPreference('civ',mode);if(mode==='setup'){step=1;stamp=current?current.id+':'+current.phase:'';}render(snapshot);};
 for(const shell of ['izlauncher','izpslauncher'])$('civ-shell-'+shell).onclick=()=>{$('civ-launcher').value=shell;render(snapshot);};
 function go(next){step=next;render(snapshot);}
 for(let i=1;i<=4;i++)$('civ-step-'+i).onclick=()=>go(i);
 for(const [id,next]of [['next-1',2],['back-2',1],['back-3',2],['back-4',3]])$('civ-'+id).onclick=()=>go(next);
 $('civ-next-1').onclick=()=>current?.loaderReady&&$('civ-save-after').checked?execute('install'):go(2);
 $('civ-next-2').onclick=()=>{if($('civ-host').value.trim()===current?.suggestedHost)go(3);else execute('set-host');};
 $('civ-input-settings').onclick=setupInput;
 $('civ-arm-installer').onclick=downloadArm;
 async function execute(action,extra={}){
  if(posting||!current)return;
  if(action==='install'&&(!$('civ-save-after').checked||!$('civ-delete-saves').checked)){$('civ-save-after').checked=true;view='setup';viewChosen=true;step=1;render(snapshot);return;}
  posting=true;render(snapshot);
  try{
   if(action==='auto-chat'&&['entered','ready','installed'].includes(current.phase)){
    if(!$('civ-prepared').checked)return;
    const reset=await act('/api/civswitch',{action:'reset',host:$('civ-host').value.trim(),confirm:true});
    if(!reset?.ok)return;
   }
   const launcher=snapshot?.civLauncher?.shell||$('civ-launcher').value;
   const auto=['auto-chat','resume-chat','watch-input','check-loader'].includes(action)&&launcher!=='original'&&!(view==='setup'&&['check-loader','watch-input'].includes(action)&&$('civ-save-after').checked);
   const result=await act('/api/civswitch',{action,host:$(view==='run'?'civ-run-host':'civ-host').value.trim(),
    networkMode:view==='run'?network.mode():undefined,launcher,launchAfterSuccess:auto,confirm:$('civ-prepared').checked,
    sameLobby:$('civ-same-lobby').checked,emptyField:$('civ-empty-field').checked,
    installAfterSuccess:['auto-chat','resume-chat'].includes(action)&&$('civ-save-after').checked,
    deleteSingleSaves:$('civ-save-after').checked&&$('civ-delete-saves').checked,...(action==='launch'||auto||extra.launchAfterSuccess?getStartup():{}),...extra});
   if(!result?.ok)return;
   if(['auto-chat','resume-chat','install'].includes(action))$('civ-delete-saves').checked=false;
   if(action==='auto-chat'){step=['entered','checking','ready','installing','installed'].includes(current?.phase)?4:3;$('civ-prepared').checked=false;}
   if(action==='check-loader'||action==='install'||action==='launch')step=4;
   if(action==='launch'){view='run';viewChosen=true;}
   if(action==='set-host')step=3;
   if(action==='discard')step=1;
  }finally{posting=false;render(snapshot);}
 }
 for(const action of ['discard','reset','auto-chat','resume-chat','prepare-recovery','check-recovery','watch-input','check-loader','install'])$('civ-'+action).onclick=()=>execute(action);
 $('civ-launch').onclick=()=>view==='setup'&&!current?.loaderReady?execute('check-loader',{automatic:true,launchAfterSuccess:true}):execute('launch');
 $('civ-use-save').onclick=()=>{view='run';viewChosen=true;civViewPreference('civ','run');render(snapshot);};
 $('civ-check-loader').onclick=()=>execute('check-loader',{automatic:true});
 $('civ-connect-ip').onclick=()=>execute('check-loader',{host:$('civ-run-host').value.trim(),automatic:false});
 $('civ-host').oninput=()=>render(snapshot);
 $('civ-stop').onclick=()=>act('/api/stop',{});
 for(const id of ['civ-prepared','civ-delete-saves','civ-save-after','civ-same-lobby','civ-empty-field'])$(id).onchange=()=>render(snapshot);
 function render(state){
  snapshot=state;
  const shown=state?.platform?.loaderId==='civswitch'&&state.platform.variant==='original';
  const enteredFlow=shown&&!wasShown;
  if(enteredFlow){step=1;stamp='';if(wasShown===false)$('civ-prepared').checked=false;}
  wasShown=shown;
  $('civswitch-flow').hidden=!shown;if(!shown)return;
  current=state.civswitch;if(!current)return;
  if(!viewChosen&&(current.phase==='installed'||current.setup?.supported===false||['starting','running'].includes(state.civLauncher?.state)))view='run';
  renderCivView(document,'civ',view,current,posting);
  const locked=posting||current.busy||state.preview||isUnavailable(),input=current.inputStatus||{};
  const save=$('civ-save-after').checked,consent=$('civ-delete-saves').checked;
  const newStamp=current.id+':'+current.phase;
  if(newStamp!==stamp){
   stamp=newStamp;
   if(!enteredFlow||current.busy||posting){
   if(['auto-typing','typing','review','recovery-ready','recovery-clearing'].includes(current.phase))step=3;
   if(['entered','checking','ready','installing','installed'].includes(current.phase))step=4;
   if(current.phase==='uncertain')step=current.index>=current.total?4:3;
   }
  }
  for(let i=1;i<=4;i++){
   $('civ-stage-'+i).hidden=view==='run'?i!==4:step!==i;$('civ-step-'+i).disabled=posting||current.busy;
   $('civ-step-'+i).classList.toggle('selected',step===i);
   $('civ-step-'+i).parentElement?.classList.toggle('current',step===i);
   $('civ-step-'+i).parentElement?.classList.toggle('done',step>i);
   $('civ-step-'+i).setAttribute('aria-current',step===i?'step':'false');
  }
  if(document.activeElement!==$('civ-host')&&hostStamp!==(current.suggestedHost||'')){
   $('civ-host').value=current.suggestedHost||'';hostStamp=current.suggestedHost||'';
   if(view!=='run'&&document.activeElement!==$('civ-run-host'))$('civ-run-host').value=current.suggestedHost||'';
  }
  const labels={idle:'Ready for setup.',chat:'Ready for input.',
   'auto-typing':'Entering chat lines and pressing A automatically. Leave the game in chat.',
   typing:'Input in progress. Leave the game in chat.',review:'Legacy manual session. Open a fresh lobby and start automatic input.',
   entered:'Input complete. Verify the game loader.',checking:'Waiting for LAN and checking the game loader.',
   ready:'Game loader ready.',installing:'Installing IZ2S.Civ7Save. Keep the game open.',
   installed:'IZ2S.Civ7Save installed. Restart the game and open Load Game.',
   'recovery-clearing':'Cancelling the partial line. No source text is sent.',
   'recovery-ready':'Confirm an empty chat field in the same lobby before continuing.',
   uncertain:'Input receipt is unconfirmed. Check the stored job before starting over.'};
  $('civ-status').textContent=t(labels[current.phase]||current.phase);
  $('civ-status').hidden=!current.busy&&(!current.error||current.restoredAttempt);
  $('civ-error').hidden=!current.error||!!current.restoredAttempt;
  $('civ-error').setAttribute('translate','no');$('civ-error').textContent=(globalThis.IZ2S_ERRORS?.canonical(current.error||'')||current.error||'');
  $('civ-previous').hidden=!current.restoredAttempt;
  $('civ-previous-error').textContent=(globalThis.IZ2S_ERRORS?.canonical(current.error||'')||current.error||'');
  $('civ-discard').disabled=locked;
  $('civ-progress').textContent=Math.min(current.index,current.total)+' / '+current.total;
  $('civ-save-consent').hidden=!save;
  $('civ-next-1').disabled=locked||(save&&!consent);$('civ-next-1').textContent=t(current.loaderReady&&save?'Install save':'Continue');
  $('civ-next-2').disabled=locked||!current.automaticInputAvailable||!$('civ-host').value.trim();
  $('civ-host').disabled=locked;$('civ-input-settings').disabled=locked;
  $('civ-arm-installer').hidden=!['ssh-gadget','local-gadget'].includes(state.platform.configuration?.inputAdapter);
  $('civ-arm-installer').disabled=isUnavailable();
  $('civ-input-status').textContent=t(!input.reachable?'Waiting for the input device.':!input.keyboard?
   'Connect the input device USB cable to Switch.':!current.automaticInputAvailable?
   'Update the input device: USB keyboard + controller required.':'Keyboard + controller ready.');
  $('civ-input-status').hidden=!!current.automaticInputAvailable;
  $('civ-auto-chat').disabled=locked||!current.automaticInputAvailable||!$('civ-prepared').checked||(save&&!consent)||!(current.phase==='idle'||current.phase==='chat'&&current.index===0||['entered','ready','installed'].includes(current.phase));
  $('civ-reset').hidden=!['uncertain','review'].includes(current.phase);
  const recovery=current.recovery||{},canRecover=current.phase==='uncertain'&&!!current.pending;
  $('civ-recovery').hidden=!canRecover&&!['recovery-ready','recovery-clearing'].includes(current.phase);
  $('civ-check-recovery').hidden=!canRecover;$('civ-check-recovery').disabled=locked;
  $('civ-same-lobby-option').hidden=!recovery.available||recovery.mode==='watch';
  $('civ-prepare-recovery').hidden=!canRecover||!recovery.available||recovery.mode==='watch';
  $('civ-prepare-recovery').disabled=locked||!$('civ-same-lobby').checked;
  $('civ-watch-input').hidden=!canRecover||recovery.mode!=='watch';$('civ-watch-input').disabled=locked;
  $('civ-empty-field-option').hidden=current.phase!=='recovery-ready';
  $('civ-resume-chat').hidden=current.phase!=='recovery-ready';
  $('civ-resume-chat').disabled=locked||!current.automaticInputAvailable||!$('civ-same-lobby').checked||!$('civ-empty-field').checked||(save&&!consent);
  $('civ-reset').disabled=locked||!$('civ-prepared').checked;
  $('civ-check-loader').hidden=!!current.loaderReady||current.phase==='installed';$('civ-check-loader').disabled=locked;
  $('civ-use-save').disabled=locked;
  renderSaveInstallation(document,'civ',current,{view,save,consent,locked});
  $('civ-install-hint').textContent=t(current.busy?'Wait for the current operation to finish.':save&&!consent&&current.loaderReady?
   'Confirm save deletion in step 1 before installation.':'Save installation requires a verified game loader.');
  $('civ-install-hint').hidden=!save||current.phase==='installed';
  $('civ-result-title').textContent=t(current.phase==='installed'?'Save installed':current.loaderReady?'Game loader ready':'Waiting for the game loader');
  $('civ-result-hint').hidden=!!current.loaderReady||current.phase==='installed';
  $('civ-result-detail').textContent=t(current.phase==='installed'?'After INSTALL DONE: restart the game and open Load Game. You may restore your system language.':current.loaderReady?'TCP 9026 verified.':'After input, the panel waits up to two minutes for LAN and verifies TCP 9026.');
  renderLauncher(document,'civ',state,current,posting,view);
  network.render(state,current,locked,view);
  $('civ-launch').hidden=state.civLauncher?.shell==='original'||view!=='run'&&step!==4;
  $('civ-app-library').hidden=view!=='run'&&step!==4;
  $('civ-back-4').hidden=view==='run';$('civ-progress').hidden=view==='run'||step<3;
  $('civ-result-detail').hidden=view==='run'&&!current.loaderReady;
  if(view==='run'&&current.loaderReady)$('civ-result-detail').textContent=t('Connected')+' · '+(current.suggestedHost||'')+':9026';
  const runtimeActive=['starting','running','offline','stopping'].includes(state.civLauncher?.state);
  $('civ-stop').hidden=!current.busy&&!runtimeActive;$('civ-stop').disabled=posting||state.civLauncher?.state==='stopping';$('civ-stop').textContent=t(state.civLauncher?.shutdownUnconfirmed?'Shutdown unconfirmed':state.civLauncher?.state==='stopping'?'Disconnecting…':runtimeActive?'Stop session':'Stop operation');
  for(const id of ['civ-prepared','civ-delete-saves','civ-save-after','civ-same-lobby','civ-empty-field'])$(id).disabled=locked;
  for(const id of ['civ-back-2','civ-back-3','civ-back-4'])$(id).disabled=posting||current.busy;
 }
 return {render};
}};

// CIV7 LAN / LDN wizard. Loaded with the existing CivSwitch UI asset.
'use strict';
globalThis.IZ2S_CIVSWITCH_LDN = {create({document, act, getStartup=()=>({}), isUnavailable=()=>false}) {
  const $ = id => document.getElementById(id), t = value => globalThis.RUN21_LANGUAGE?.t(value) || value;
  let snapshot = null, current = null, posting = false, step = 1, phaseStamp = '', formStamp = '', wasShown=null,view='setup',viewChosen=false;
  [view,viewChosen]=civViewPreference('ldn');
  const network=civRunNetwork(document,'ldn',()=>render(snapshot));
  $('ldn-run-host').oninput=()=>render(snapshot);
  for(const mode of ['setup','run'])$('ldn-view-'+mode).onclick=()=>{view=mode;viewChosen=true;civViewPreference('ldn',mode);if(mode==='setup'){step=1;phaseStamp=current?current.id+':'+current.phase:'';}render(snapshot);};
  for(const shell of ['izlauncher','izpslauncher'])$('ldn-shell-'+shell).onclick=()=>{$('ldn-launcher').value=shell;render(snapshot);};
  function go(next) {step = next; render(snapshot);}
  for (let i = 1; i <= 4; i++) $('ldn-step-' + i).onclick = () => go(i);
  for (const [id, next] of [['back-2', 1], ['back-3', 2], ['back-4', 3]])
    $('ldn-' + id).onclick = () => go(next);
  function values() {
    return {host: $(view==='run'?'ldn-run-host':'ldn-host').value.trim(), networkMode:view==='run'?network.mode():undefined, phy: $('ldn-phy').value,
      hostMac: $('ldn-host-mac').value.trim(), chatDelay: Number($('ldn-chat-delay').value),
      handshakeScale: Number($('ldn-handshake-scale').value)};
  }
  async function execute(action, extra = {}) {
    if (posting || !current) return;
    if(action==='install'&&(!$('ldn-save-after').checked||!$('ldn-delete-saves').checked)){$('ldn-save-after').checked=true;view='setup';viewChosen=true;step=1;render(snapshot);return;}
    posting = true; render(snapshot);
    try {
      const launcher=snapshot?.civLauncher?.shell||$('ldn-launcher').value;
      const auto=['start','check-loader'].includes(action)&&launcher!=='original'&&!(view==='setup'&&action==='check-loader'&&$('ldn-save-after').checked);
      const body = {action, launcher,launchAfterSuccess:auto, ...(['configure', 'start', 'check-loader', 'install'].includes(action) ? values() : {}),
        confirm: $('ldn-prepared').checked, freshLobby: $('ldn-prepared').checked,
        installAfterSuccess: action === 'start' && $('ldn-save-after').checked,
        deleteSingleSaves: $('ldn-save-after').checked && $('ldn-delete-saves').checked, ...(action==='launch'||auto||extra.launchAfterSuccess?getStartup():{}), ...extra};
      const result = await act('/api/civswitch-ldn', body);
      if (!result?.ok) return;
      if(action==='refresh'&&result.prepared===true)step=2;
      if (action === 'configure') step = 3;
      if (action === 'discard') {step = 1;view='setup';viewChosen=true;civViewPreference('ldn','setup');$('ldn-prepared').checked=false;$('ldn-delete-saves').checked=false;}
      if (action === 'check-loader' || action === 'install'||action==='launch') step = 4;
      if(action==='launch'){view='run';viewChosen=true;}
      if (action === 'start' || action === 'install') {
        $('ldn-prepared').checked = false; $('ldn-delete-saves').checked = false;
      }
    } finally {posting = false; render(snapshot);}
  }
  $('ldn-launch').onclick=()=>view==='setup'&&!current?.loaderReady?execute('check-loader',{automatic:true,launchAfterSuccess:true}):execute('launch');
  $('ldn-next-2').onclick = () => execute('configure');
  $('ldn-next-1').onclick = () => execute(current?.loaderReady&&$('ldn-save-after').checked?'install':'refresh');
  $('ldn-use-save').onclick=()=>{view='run';viewChosen=true;civViewPreference('ldn','run');render(snapshot);};
  for (const action of ['discard', 'prepare', 'refresh', 'start', 'check-loader', 'install'])
    $('ldn-' + action).onclick = () => execute(action);
  $('ldn-check-loader').onclick=()=>execute('check-loader',{automatic:true});
  $('ldn-connect-ip').onclick=()=>execute('check-loader',{host:$('ldn-run-host').value.trim(),automatic:false});
  $('ldn-stop').onclick = () => act('/api/stop', {});
  $('ldn-copy-cap').onclick=async()=>{
   const field=$('ldn-cap-command');if(!field.value)return;
   try{const clipboard=document.defaultView?.navigator?.clipboard||globalThis.navigator?.clipboard;if(!clipboard)throw Error('Clipboard unavailable');await clipboard.writeText(field.value);$('ldn-copy-status').textContent=t('Command copied.');}
   catch{field.focus?.();field.select?.();$('ldn-copy-status').textContent=t('Select the command and copy it with Ctrl+C.');}
  };
  for (const id of ['ldn-prepared', 'ldn-save-after', 'ldn-delete-saves']) $(id).onchange = () => render(snapshot);
  $('ldn-host').oninput=()=>render(snapshot);
  $('ldn-keys-replace').onclick=()=>$('ldn-keys-file').click();
  $('ldn-keys-file').onchange = async () => {
    const file = $('ldn-keys-file').files?.[0];
    if (!file) return;
    try {
      if (file.size > 65536) { $('ldn-keys-status').textContent = t('Choose a valid prod.keys file smaller than 64 KB'); return; }
      await execute('import-keys', {content: await file.text()});
    } finally { $('ldn-keys-file').value = ''; }
  };
  function render(state) {
    snapshot = state;
    const shown = state?.platform?.loaderId === 'civswitch-ldn' && state.platform.variant === 'original';
    const enteredFlow=shown&&!wasShown;
    if(enteredFlow){step=1;phaseStamp='';formStamp='';if(wasShown===false)$('ldn-prepared').checked=false;}
    wasShown=shown;
    $('civswitch-ldn-flow').hidden = !shown;
    if (!shown || !state.civswitchLdn) return;
    current = state.civswitchLdn;
    if(!viewChosen&&(current.phase==='installed'||current.setup?.supported===false||['starting','running'].includes(state.civLauncher?.state)))view='run';
    renderCivView(document,'ldn',view,current,posting);
    const setup = current.setup || {}, locked = posting || current.busy || state.preview || isUnavailable();
    const save = $('ldn-save-after').checked, consent = $('ldn-delete-saves').checked;
    const stamp = current.id + ':' + current.phase;
    if (phaseStamp !== stamp) {
      phaseStamp = stamp;
      if(!enteredFlow||current.busy||posting){
      if (['sending', 'restoring', 'stopping'].includes(current.phase)) step = 3;
      if (['checking', 'ready', 'entered', 'installing', 'installed'].includes(current.phase)) step = 4;
      if (current.phase==='interrupted'||current.phase==='entered'&&current.error&&current.networkRestored===false) {step=current.retryStep===1||current.networkRestored===false?1:3;$('ldn-prepared').checked=false;$('ldn-delete-saves').checked=false;}
      }
    }
    for (let i = 1; i <= 4; i++) {
      $('ldn-stage-' + i).hidden = view==='run'?i!==4:step !== i;
      const button = $('ldn-step-' + i);
      button.disabled = posting || current.busy;
      button.classList.toggle('selected', step === i);
      button.parentElement?.classList.toggle('current', step === i);
      button.parentElement?.classList.toggle('done', step > i);
      button.setAttribute('aria-current', step === i ? 'step' : 'false');
    }
    const fields = JSON.stringify([current.suggestedHost, current.phy, current.hostMac, current.chatDelay, current.handshakeScale, setup.radios]);
    const editing = ['ldn-host', 'ldn-phy', 'ldn-host-mac', 'ldn-chat-delay', 'ldn-handshake-scale'].some(id => document.activeElement === $(id));
    if (formStamp !== fields && !editing) {
      formStamp = fields;
      $('ldn-host').value = current.suggestedHost || '';
      if(view!=='run'&&document.activeElement!==$('ldn-run-host'))$('ldn-run-host').value=current.suggestedHost||'';
      $('ldn-phy').replaceChildren();
      for (const phy of setup.radios || []) {
        const option = document.createElement('option'); option.value = phy; option.textContent = phy; $('ldn-phy').append(option);
      }
      $('ldn-phy').value = current.phy || '';
      $('ldn-host-mac').value = current.hostMac || '';
      $('ldn-chat-delay').value = current.chatDelay ?? 10;
      $('ldn-handshake-scale').value = current.handshakeScale ?? 1;
    }
    const labels = {idle: 'Complete PC setup before sending.', preparing: 'Preparing PC dependencies. Keep this window open.',
      sending: 'Sending commands over LDN. Leave the wireless lobby open.', restoring: 'Restoring the selected Wi-Fi radio.',
      stopping: 'Stopping delivery and waiting for Wi-Fi restoration.', checking: 'Checking the game loader on TCP 9026.',
      ready: 'Game loader ready.', entered: 'Delivery finished. Check the console address and game loader.',
      installing: 'Installing IZ2S.Civ7Save. Keep the game open.', installed: 'IZ2S.Civ7Save installed. Restart the game and open Load Game.',
      interrupted: 'Operation interrupted. Check Wi-Fi and the loader before using a fresh lobby.'};
    $('ldn-status').textContent = t(labels[current.phase] || current.phase);
    $('ldn-status').hidden=!current.busy;
    $('ldn-previous').hidden=!current.restoredAttempt;
    $('ldn-previous-error').textContent=(globalThis.IZ2S_ERRORS?.canonical(current.error||'')||current.error||'');
    const launcherOwnsSession=['starting','running','offline','stopping'].includes(state.civLauncher?.state);
    $('ldn-discard').hidden=!current.error&&!current.restoredAttempt&&!current.index&&['idle','preparing'].includes(current.phase);
    $('ldn-discard').disabled=locked||launcherOwnsSession;
    $('ldn-discard').textContent=t('Start over');
    $('ldn-error').hidden = !current.error||!!current.restoredAttempt; $('ldn-error').setAttribute('translate','no');$('ldn-error').textContent = (globalThis.IZ2S_ERRORS?.canonical(current.error||'')||current.error||'');
    $('ldn-progress').textContent = Math.min(current.index, current.total) + ' / ' + current.total;
    $('ldn-dependencies-status').textContent = t(!setup.supported ? 'CIV7 LAN requires native Linux' :
      !setup.radios?.length ? 'No PC Wi-Fi radio found.' : !setup.dependenciesReady ? 'Prepare PC dependencies in step 1' :
      !setup.networkManager ? 'NetworkManager is required.' : !setup.capabilitiesReady ? 'Enable network permissions using the command below.' : 'PC dependencies and network permissions are ready.');
    $('ldn-keys-field').hidden=!!setup.keysReady;
    $('ldn-keys-stored').hidden=!setup.keysReady;
    $('ldn-keys-replace').disabled=locked;
    $('ldn-keys-status').textContent = t(setup.keysReady ? 'prod.keys added. Stored locally; never included in logs.' : 'Required: add your own prod.keys from Switch 1 before starting.');
    $('ldn-prepare').hidden = setup.dependenciesReady === true;
    $('ldn-prepare').disabled = locked || !setup.supported;
    $('ldn-permissions').hidden = !setup.supported;
    $('ldn-permissions-status').textContent=t(setup.capabilitiesReady?'Enabled':'Required');
    $('ldn-permissions-note').textContent=t(!setup.dependenciesReady?'Prepare dependencies first, then run this command once in the PC terminal.':setup.capabilitiesReady?'Permissions are enabled. Keep this command for reinstalling or moving the panel.':'Copy the command into the PC terminal, run it, then press Continue.');
    $('ldn-copy-cap').disabled=!setup.capabilityCommand;
    $('ldn-cap-command').value = setup.capabilityCommand || '';
    $('ldn-save-consent').hidden = !save;
    $('ldn-next-1').textContent=t(current.loaderReady&&save?'Install save':'Continue');
    $('ldn-next-1').disabled = locked || !setup.dependenciesReady || !setup.keysReady || !setup.radios?.length || save && !consent;
    $('ldn-next-2').disabled = locked || !current.prepared;
    $('ldn-start').textContent=t(current.phase==='interrupted'?'Retry delivery':'Send over LDN');
    $('ldn-start').disabled = locked || !current.prepared || !$('ldn-prepared').checked || save && !consent;
    $('ldn-check-loader').hidden=!!current.loaderReady;
    $('ldn-check-loader').disabled = locked;
    $('ldn-use-save').disabled=locked;
    renderSaveInstallation(document,'ldn',current,{view,save,consent,locked});
    renderLauncher(document,'ldn',state,current,posting,view);
      $('ldn-launch').hidden=state.civLauncher?.shell==='original'||view!=='run'&&step!==4;
  $('ldn-app-library').hidden=view!=='run'&&step!==4;
    $('ldn-back-4').hidden=view==='run';$('ldn-progress').hidden=view==='run'||step<3;
    $('ldn-result-detail').hidden=view==='run'&&!current.loaderReady;
    const runtimeActive=['starting','running','offline','stopping'].includes(state.civLauncher?.state);
    $('ldn-stop').hidden=!current.busy&&!runtimeActive;$('ldn-stop').disabled=posting||state.civLauncher?.state==='stopping';$('ldn-stop').textContent=t(state.civLauncher?.shutdownUnconfirmed?'Shutdown unconfirmed':state.civLauncher?.state==='stopping'?'Disconnecting…':runtimeActive?'Stop session':'Stop operation');
    $('ldn-result-title').textContent = t(current.phase === 'installed' ? 'Save installed' : current.loaderReady ? 'Game loader ready' : 'Waiting for the game loader');
    $('ldn-result-hint').hidden=!!current.loaderReady||current.phase==='installed';
    $('ldn-result-detail').textContent = t(current.phase === 'installed' ? 'After INSTALL DONE: restart the game and open Load Game. You may restore your system language.' :
      current.loaderReady ? 'TCP 9026 verified.' : 'After delivery, Wi-Fi is restored and TCP 9026 is checked automatically.');
    if(view==='run'&&current.loaderReady)$('ldn-result-detail').textContent=t('Connected')+' · '+(current.suggestedHost||'')+':9026';
    for (const id of ['ldn-refresh', 'ldn-keys-file', 'ldn-prepared', 'ldn-save-after', 'ldn-delete-saves', 'ldn-phy', 'ldn-host', 'ldn-host-mac', 'ldn-chat-delay', 'ldn-handshake-scale', 'ldn-back-2', 'ldn-back-3', 'ldn-back-4']) $(id).disabled = locked;
    network.render(state,current,locked,view);
  }
  return {render};
}};

function renderSaveInstallation(document,prefix,current,{view,save,consent,locked}){
 const $=id=>document.getElementById(prefix+'-'+id),t=value=>globalThis.RUN21_LANGUAGE?.t(value)||value;
 const installed=current.phase==='installed'||current.saveInstallation?.state==='installed';
 const status=$('save-status');
 if(status){status.textContent=t(current.phase==='installing'?'Installing IZ2S.Civ7Save. Keep the game open.':current.phase==='installed'?'Save installed. Restart Civilization VII, open Load Game, then use Launch in this panel.':installed?'Save installed. Next time: Civilization VII → Load Game, then Launch in this panel.':'Save installation is not confirmed. Install IZ2S.Civ7Save in First Setup to use Load Game next time.');status.classList.toggle('save-confirmed',installed);}
 $('install').hidden=view==='run'||installed;
 $('install').disabled=locked||!current.loaderReady;
 $('install').textContent=t(save&&consent?'Install save':'Review save installation');
}

function renderLauncher(document,prefix,state,current,posting,view){
 const $=id=>document.getElementById(prefix+'-'+id),t=value=>globalThis.RUN21_LANGUAGE?.t(value)||value;
 const runtime=state.civLauncher||{};if(['izlauncher','izpslauncher'].includes(runtime.shell))$('launcher').value=runtime.shell;
 const modules=document.getElementById('launch-modules'),flow=document.getElementById(prefix==='ldn'?'civswitch-ldn-flow':'civswitch-flow');
 if(modules&&flow){const bar=flow.querySelector('.civ-action-bar');if(modules.parentElement!==flow||modules.nextElementSibling!==bar)flow.insertBefore(modules,bar);modules.hidden=$('stage-4').hidden||runtime.shell==='original';}
 const automatic=runtime.shell!=='original'&&(view==='run'||!$('save-after').checked);
 $('check-loader').hidden=!!current.loaderReady||(view==='setup'&&runtime.shell!=='original');
 $('check-loader').textContent=t(automatic?'Connect and start launcher':'Connect automatically');$('check-loader').classList.toggle('primary',automatic&&view==='run');
 const running=runtime.state==='running',active=['running','offline','starting','stopping'].includes(runtime.state);
 $('launcher-panel').hidden=runtime.shell==='original';
 $('launcher').disabled=posting||current.busy;
 const connecting=!active&&((current.busy&&['checking','entered'].includes(current.phase))||(runtime.automaticLaunch&&!current.busy));
 $('launch').disabled=active||posting||current.busy||!!runtime.automaticLaunch||(view==='run'&&!current.loaderReady)||!!state.preview;
 $('launch').textContent=t(connecting?'Connecting to game loader…':view==='setup'&&!current.loaderReady?'Connect and start launcher':'Start launcher');
 $('launch').setAttribute('aria-busy',String(connecting));
 $('launch').title=connecting?t('Delivery is complete. Looking for the game loader; the selected launcher will start automatically. Use Stop operation to cancel.'):'';
  const offline=runtime.state==='offline',stopping=runtime.state==='stopping',unconfirmed=runtime.shutdownUnconfirmed===true;
 const phase=unconfirmed?'Shutdown unconfirmed':stopping?'Disconnecting…':offline?'Connection interrupted':'Preparing the Civ7 launcher.';
 $('launcher-status').hidden=runtime.state==='starting'&&!runtime.error;
 if(active){$('status').hidden=running;$('status').textContent=t(phase);}

 for(const id of ['run-instructions','result-hint'])if($(id))$(id).classList.toggle('session-obsolete',running);
 $('launcher-status').textContent=t(connecting?'Delivery is complete. Looking for the game loader; the selected launcher will start automatically. Use Stop operation to cancel.':(runtime.error?globalThis.IZ2S_ERRORS?.canonical(runtime.error)||runtime.error:null)|| (running?'Launcher ready. Open App library or press X on the console for App Store.':offline?'Connection lost. Stop this session, restart Civilization VII and open Load Game.':unconfirmed?'Restart Civilization VII before starting a new session.':stopping?'Disconnecting…':active?'Preparing the Civ7 launcher.':runtime.needsRestart?'Restart Civilization VII and open Load Game before another graphics session':automatic?'The selected launcher starts automatically after connection.':'Applications load into session memory through the PC.'));
}

function renderCivView(document,prefix,view,current,posting){
 const $=id=>document.getElementById(prefix+'-'+id);
 $('setup-steps').hidden=view==='run';$('run-connection').hidden=view!=='run';
 for(const mode of ['setup','run']){$('view-'+mode).classList.toggle('selected',mode===view);$('view-'+mode).setAttribute('aria-pressed',String(mode===view));$('view-'+mode).disabled=posting||current.busy;}
 for(const shell of ['izlauncher','izpslauncher']){$('shell-'+shell).classList.toggle('selected',$('launcher').value===shell);$('shell-'+shell).setAttribute('aria-pressed',String($('launcher').value===shell));$('shell-'+shell).disabled=posting||current.busy;}
 $('run-host').disabled=posting||current.busy;
 $('connect-ip').disabled=posting||current.busy||!$('run-host').value.trim();
 $('check-loader').textContent=(globalThis.RUN21_LANGUAGE?.t|| (s=>s))('Connect automatically');
 $('mode-help').hidden=view==='run';
}

function civRunNetwork(document,prefix,changed){
 const $=id=>document.getElementById(prefix+'-'+id);
 let mode=null,chosen=false,snapshot=null,verifiedStamp='',addresses={lan:'',wifi:''};
 function matches(current){
  const peer=snapshot?.connections?.devices?.lan,lan=peer?.addresses?.lan||peer?.lastKnownAddresses?.lan?.address;
  const verifiedMode=current.networkMode||(current.suggestedHost===lan?'lan':'wifi');
  return !!current.loaderReady&&verifiedMode===mode&&(!$('run-host').value.trim()||$('run-host').value.trim()===current.suggestedHost);
 }
 function paint(view){
  for(const value of ['lan','wifi']){
   const button=$('run-'+value),source=$('source-'+value);
   button.classList.toggle('selected',mode===value);button.setAttribute('aria-pressed',String(mode===value));
   if(source){source.classList.toggle('selected',mode===value);source.setAttribute('aria-pressed',String(mode===value));source.disabled=button.disabled;}
  }
  if($('run-network').dataset)$('run-network').dataset.mode=mode;
  const endpoint=$('run-endpoint'),home=$('run-connection'),card=document.getElementById('device-lan');
  if(endpoint&&home){const target=view==='run'?card?.querySelector('.device-controls'):home;if(target&&endpoint.parentElement!==target)target.append(endpoint);endpoint.hidden=view!=='run';}
  if(card)card.classList.toggle('civ-run-console',view==='run');
  const server=document.getElementById('device-server'),source=$('run-source');
  if(source){
   const target=view==='run'?server:home;
   if(target?.append&&source.parentElement!==target)target.append(source);
   source.hidden=view!=='run';
  }
  const serverTransport=server?.querySelector('.transport-choice');
  if(serverTransport)serverTransport.hidden=view==='run';
  if(server){
   // Launch uses this running PC service as a TCP client. LDN radio, keys and
   // helper permissions are First Setup prerequisites, not PC availability.
   // Keep the remote loader's independent readiness on the Console card.
   const check=snapshot?.checks?.items?.find(row=>row.id==='server');
   const ready=view==='run'||!!check?.ready,pending=view!=='run'&&!!check?.pending;
   server.classList.toggle('ready',ready);server.classList.toggle('pending',pending);
   const t=globalThis.RUN21_LANGUAGE?.t||(s=>s),label=server.querySelector('.state');
   if(label)label.textContent=t(ready?(pending?'WAITING':'✓ READY'):'○ WAITING');
   const detail=server.querySelector('.device-detail');
   if(detail)detail.textContent=t(view==='run'?'PC and Switch must be on the same local network.':check?.detail||'Complete PC setup and add prod.keys in step 1.');
  }
  for(const badge of document.querySelectorAll?.('#devices .ldn-transport')||[]){
   const active=view==='run'?mode:'ldn',transport=badge.parentElement;
   const icon=transport.querySelector('[data-transport="'+(active==='lan'?'lan':'wifi')+'"] svg');
   if(icon&&badge.querySelector('svg'))badge.querySelector('svg').replaceWith(icon.cloneNode(true));
   const text=badge.querySelector('span');if(text)text.textContent=active==='ldn'?'Wi-Fi · LDN':active==='lan'?'LAN · TCP 9026':'Wi-Fi · TCP 9026';
  }
 }
 for(const value of ['lan','wifi'])$('run-'+value).onclick=()=>{
  if(mode)addresses[mode]=$('run-host').value.trim();mode=value;chosen=true;
  const peer=snapshot?.connections?.devices?.lan;
  $('run-host').value=addresses[value]||peer?.addresses?.[value]||peer?.lastKnownAddresses?.[value]?.address||'';
  changed();
 };
 for(const value of ['lan','wifi'])if($('source-'+value))$('source-'+value).onclick=()=>$('run-'+value).onclick();
 return {mode:()=>mode||'wifi',matches,render(state,current,locked,view){
  snapshot=state;
  const peer=state.connections?.devices?.lan,host=$('run-host').value.trim();
  const lan=peer?.addresses?.lan||peer?.lastKnownAddresses?.lan?.address;
  if(!chosen)mode=host&&host===lan?'lan':['lan','wifi'].includes(current.networkMode)?current.networkMode:'wifi';
  if(host)addresses[mode]=host;
  const verified=current.loaderReady?current.suggestedHost+':'+(current.networkMode||mode):'';
  if(verified&&verified!==verifiedStamp&&document.activeElement!==$('run-host')&&(!current.networkMode||current.networkMode===mode)){$('run-host').value=current.suggestedHost;addresses[mode]=current.suggestedHost;}
  verifiedStamp=verified;
  for(const value of ['lan','wifi'])$('run-'+value).disabled=locked;
  paint(view);
  $('connect-ip').disabled=locked||!$('run-host').value.trim();
  const paired=view==='run'?matches(current):!!current.loaderReady;
  const consoleCard=document.getElementById('device-lan');
  if(consoleCard){
   // An unopened loader is an expected waiting state, not a lost console session.
   const waiting=!paired&&!state.civLauncher?.shutdownUnconfirmed&&state.civLauncher?.state!=='offline';
   consoleCard.classList.toggle('awaiting-loader',waiting);consoleCard.classList.toggle('ready',paired);consoleCard.classList.toggle('pending',false);
   const label=consoleCard.querySelector('.state');if(label&&(waiting||paired))label.textContent=(globalThis.RUN21_LANGUAGE?.t||(s=>s))(paired?'✓ READY':'AWAITING LOADER');
   const detail=consoleCard.querySelector('.detail-text')||consoleCard.querySelector('.device-detail');
   if(waiting&&view!=='run'&&detail)detail.textContent=(globalThis.RUN21_LANGUAGE?.t||(s=>s))('Connects after setup.');
   const help=consoleCard.querySelector('.detail-help');if(waiting&&help)help.hidden=true;
  }
  if(view==='run'){
   const paired=matches(current),t=value=>globalThis.RUN21_LANGUAGE?.t(value)||value;
   $('check-loader').hidden=paired;
   $('launch').disabled=$('launch').disabled||!paired;
   if(!paired)$('result-title').textContent=t('Connect Switch');
   $('result-detail').hidden=!paired;
   $('result-hint').hidden=paired;
   if(current.networkMode&&current.networkMode!==mode)$('error').hidden=true;
   const consoleCard=document.getElementById('device-lan');
   if(consoleCard){consoleCard.classList.toggle('ready',paired);consoleCard.classList.toggle('pending',!paired);const label=consoleCard.querySelector('.state');if(label)label.textContent=t(paired?'✓ READY':current.error&&!current.restoredAttempt?'○ WAITING':'AWAITING LOADER');const detail=consoleCard.querySelector('.detail-text')||consoleCard.querySelector('.device-detail');if(detail)detail.textContent=t(paired?'Remote JS Loader verified on TCP 9026':'Open Load Game on Switch, then connect here.');const help=consoleCard.querySelector('.detail-help');if(help)help.hidden=true;}
  }
 }};
}

function civViewPreference(prefix,value){
 try{const storage=globalThis.localStorage,key='iz2s.civ7.view.'+prefix;if(value)storage?.setItem(key,value);const selected=value||storage?.getItem(key);if(['setup','run'].includes(selected))return [selected,true];}catch{}
 return ['setup',false];
}
