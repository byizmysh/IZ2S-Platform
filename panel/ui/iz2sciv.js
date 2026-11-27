'use strict';
globalThis.IZ2S_IZ2SCIV={setupStep(current){
 const phase=current?.phase;
 if(current?.busy){
  if(['prepare','permissions'].includes(phase))return 2;
  if(phase==='install')return 4;
  if(['launch','check-launcher'].includes(phase))return 5;
  return 3;
 }
 if(phase==='confirm-sd-done')return 2;
 if(['prepare-done','permissions-done'].includes(phase))return 2;
 if(['start-done','check-loader-done','find-addresses-done','select-host-done'].includes(phase))return current?.loaderReady?4:3;
 if(['failed','interrupted'].includes(phase)&&current?.lastAction){
  return {prepare:2,permissions:2,install:4,launch:5,'check-launcher':5}[current.lastAction]||3;
 }
 if(current?.installed)return 5;
 if(current?.loaderReady||phase==='install-done')return 4;
 if(!current?.sdReady)return 1;
 return current?.prepared?3:2;
},pcAction(current){
 if(current?.hostPlatform==='win32')return 'continue';
 const setup=current?.setup||{};
 if(!setup.supported)return 'unsupported';
 if(!setup.keysReady)return 'keys';
 if(!setup.dependenciesReady)return 'prepare';
 if(!setup.capabilitiesReady)return 'permissions';
 if(!setup.networkManager||!setup.radios?.length)return 'refresh';
 if(!setup.radios.includes(current.phy))return 'radio';
 return 'continue';
},create({document,act,downloadSd,downloadWifiReport=()=>{},isUnavailable=()=>false,isPanelBusy=()=>false,showLog=()=>{},getLink=()=>null,openLibrary=()=>{},openFiles=()=>{},openFtp=()=>{},refreshLink=()=>{},resetSession=()=>{},getFtp=()=>null,setPcLink=()=>{},closeFtp=()=>{}}){
 const $=id=>document.getElementById('iz2sciv-'+id),t=value=>globalThis.RUN21_LANGUAGE?.t(value)||value;
 let snapshot=null,current=null,step=1,view='setup',posting=false,stamp='',radioStamp='',initialized=false;
 let actionError='',wasOnline=false,setupChosen=false,launchContext='';
 let resettingSession=false, freshRun=false;
 let downloading=false,addressStamp='',wifiStamp='',keyLoading=false,keyName='',keyError='',keyImported=false;
 const phases={idle:'Ready for setup.',prepare:'Preparing PC dependencies…',permissions:'Waiting for network permissions…',
  start:'Connecting to the console…','restore-network':'Restoring PC Wi-Fi…','restore-network-done':'PC Wi-Fi restored.',
  'check-loader':'Finding the game loader…',install:'Building and installing the IZ2SCiv save…',launch:'Starting IZ2S core…',
  'check-launcher':'Checking IZ2S FTP…','prepare-done':'PC dependencies prepared.','permissions-done':'Network permissions enabled.',
  'confirm-sd-done':'SD copy confirmed. Continue with PC setup.','start-done':'Game loader found. Ready to install the launch save.',
  'check-loader-done':'Game loader found. Ready to install the launch save.','install-done':'IZ2SCiv save installed. Continue with Launch.',
  'launch-done':'IZ2S core started. Follow the console steps to open the launcher.',
  'find-addresses':'Finding the game loader…','find-addresses-done':'Game loader found. Ready to install the launch save.',
  'select-host':'Checking the selected console address…','select-host-done':'Selected address verified. It will be checked again before sending.',
  'auto-host-done':'Automatic console detection is enabled.',
  'check-launcher-done':'IZ2S FTP is available and the required files are present.',failed:'Operation failed.',interrupted:'Operation stopped.'};
 function text(id,value){const translated=t(value);if($(id).textContent!==translated)$(id).textContent=translated;}
 function focusStep(){
  const target=[3,4].includes(step)&&!$('stage-4').hidden?$('stage-4'):$('stage-'+step);
  target.querySelector('h3').focus({preventScroll:true});
 }
 function go(next){
  if(view==='setup')setupChosen=true;
  if(next===3&&step!==3)$('lobby').checked=false;
  step=next;
  render(snapshot);
  focusStep();
 }
 function mode(next){
  if(posting||current?.busy||isPanelBusy())return;
  view=next;
  setupChosen=next==='setup';
  if(next==='run')document.getElementById('session-activity').open=false;
  if(next==='run')freshRun=!getLink()?.online;
  if(freshRun){$('lobby').checked=false;}
  go(next==='run'?5:1);
 }
 // Launch is a read-only guide. A fresh installation always starts at step 1.
 for(const i of [1,2,3,5])$('step-'+i).onclick=()=>{if(view==='setup'&&!$('step-'+i).disabled)go(i);};
 $('restart-run').onclick=()=>mode('setup');
 for(const button of document.querySelectorAll('[data-iz2sciv-next]'))button.onclick=()=>go(Number(button.dataset.iz2scivNext));
 $('view-setup').onclick=()=>mode('setup');$('view-run').onclick=()=>mode('run');
 async function execute(action,extra={},options={}){
  if(posting||resettingSession||current?.busy||isPanelBusy()||isUnavailable()||snapshot?.preview)return;
  posting=true;actionError='';render(snapshot);
  try{
   const result=await act('/api/iz2sciv',{action,...extra},{...options,onError:error=>{actionError=error.message;options.onError?.(error);}});
   if(result?.ok&&action==='start')$('lobby').checked=false;
   return result;
  }catch(error){actionError=error.message;options.onError?.(error);}
  finally{posting=false;render(snapshot);}
 }
 for(const action of ['refresh','find-addresses','auto-host','restore-network'])$(action).onclick=()=>execute(action);
 $('windows-refresh').onclick=()=>execute('wifi-status');
 $('windows-report').onclick=()=>{if(!$('windows-report').disabled)return downloadWifiReport(current.windowsWifi);};
 $('windows-find').onclick=()=>{if(!$('windows-find').disabled)return execute('check-loader');};
 $('open-log').onclick=()=>showLog();
 $('installed-next').onclick=()=>{if(current?.installed)go(5);};
 $('pc-action').onclick=()=>{
  if(snapshot?.preview){go(3);return;}
  if(posting||keyLoading||current?.busy||isPanelBusy()||isUnavailable())return;
  const action=globalThis.IZ2S_IZ2SCIV.pcAction(current);
  if(action==='keys')$('keys').click();
  else if(action==='continue')go(3);
  else if(action==='radio')$('radio').focus();
  else if(action!=='unsupported')execute(action);
 };
 $('keys-replace').onclick=()=>$('keys').click();
 $('select-host').onclick=()=>execute('select-host',{host:$('host-options').value});
 $('manual-check').onclick=()=>execute('select-host',{host:$('manual-host').value.trim()});
 $('manual-host').oninput=()=>render(snapshot);
 $('host-options').onchange=()=>render(snapshot);
 $('download-sd').onclick=async()=>{
  if(downloading)return;downloading=true;text('download-status','Preparing the SD archive…');render(snapshot);
  try{await downloadSd();text('download-status','ZIP download started. Extract it to the SD card root.');}
  catch(error){actionError=error.message;text('download-status',error.message);}
  finally{downloading=false;render(snapshot);}
 };
 $('confirm-sd').onclick=async()=>{
  if($('confirm-sd').disabled||!$('sd-copied').checked)return;
  if(snapshot?.preview){go(2);return;}
  const result=await execute('confirm-sd',{copied:true});
  // Repeating confirmation after going back or resetting returns the same phase.
  if(result?.ok&&view==='setup'&&(step===1||step===2))go(2);
 };
 $('start').onclick=()=>{
  if(![3,4].includes(step)||$('start').disabled||!$('lobby').checked)return;
  return execute('start',{confirm:true,installAfterSuccess:true});
 };
 $('connection-reset').onclick=async()=>{
  if($('connection-reset').disabled)return;
  const result=await execute('reset-connection');
  if(result?.ok){freshRun=false;$('lobby').checked=false;go(3);}
 };
 $('install').onclick=()=>{if(!$('install').disabled)return execute('install',{replaceTrigger:true});};
 $('library').onclick=()=>openLibrary();
 $('files').onclick=()=>{const link=getLink();return link?.online&&link.files?openFiles():openFtp({reuse:true});};
 $('link-refresh').onclick=()=>refreshLink();
 $('pc-toggle').onclick=()=>setPcLink(getLink()?.enabled===false);
 $('ftp-open').onclick=()=>openFtp();
 $('ftp-close').onclick=()=>closeFtp();
 $('reset-session').onclick=async()=>{
  if($('reset-session').disabled)return;
  resettingSession=true;
  render(snapshot);
  try{
   if(!(await resetSession())?.ok)return;
   for(const id of ['lobby','sd-copied'])$(id).checked=false;
   actionError='';
   mode('setup');
  }finally{
   resettingSession=false;
   render(snapshot);
  }
 };
 $('stop').onclick=()=>act('/api/stop',{});
 $('radio').onchange=()=>execute('configure',{phy:$('radio').value});
 $('keys').onchange=async()=>{
  const file=$('keys').files[0];if(!file)return;
  keyLoading=true;keyError='';keyImported=false;render(snapshot);
  try{
   if(file.size>65536)throw Error('Choose a prod.keys file smaller than 64 KB.');
   const result=await execute('import-keys',{content:await file.text()},{onError:error=>{keyError=error.message;}});
   if(result?.ok){keyName=file.name;keyImported=true;}
   else keyError||='The file was not imported. Choose a valid prod.keys and try again.';
  }catch(error){keyError=error.message;actionError=error.message;}
  finally{keyLoading=false;$('keys').value='';render(snapshot);}
 };
 for(const id of ['lobby','sd-copied'])$(id).onchange=()=>render(snapshot);
 function render(state){
  const previousStep=step,wasInitialized=initialized;
  snapshot=state;
  const shown=state?.platform?.loaderId==='iz2sciv-core';$('flow').hidden=!shown;$('result').hidden=!shown;if(!shown)return;
  current=state.iz2sciv;if(!current)return;
  // A preview opens a fresh guide, independent of the live console's last phase.
  if(state.preview)current={...current,phase:'preview',id:'preview',busy:false,installed:false,needsSaveUpdate:false,loaderReady:false,error:null,networkRecoveryAvailable:false,host:'',selectedHost:'',addressOptions:[]};
  if(!initialized){
   initialized=true;step=state.preview?1:globalThis.IZ2S_IZ2SCIV.setupStep(current);
   if(step===5){
    view='run';document.getElementById('session-activity').open=false;
    if(!current.busy&&!getLink()?.online){freshRun=true;}
   }
  }
  const serverOffline=isUnavailable();
  const working=posting||keyLoading||resettingSession||current.busy||isPanelBusy();
  const locked=working||state.preview||serverOffline,setup=current.setup||{};
  const operationStamp=JSON.stringify([current.id,current.phase,current.busy]);
  if(stamp!==operationStamp){
   stamp=operationStamp;
   if(wasInitialized&&(current.busy||['start-done','check-loader-done','find-addresses-done','select-host-done'].includes(current.phase)))freshRun=false;
   const next=current.busy?globalThis.IZ2S_IZ2SCIV.setupStep(current):
    {'confirm-sd-done':2,'start-done':current.loaderReady?4:3,'check-loader-done':current.loaderReady?4:3,'find-addresses-done':current.loaderReady?4:3,'select-host-done':current.loaderReady?4:3,'install-done':current.installed?5:4}[current.phase];
   if(next&&!freshRun){step=next;if([3,4].includes(next))view='setup';}
   if(current.error&&step>=3&&current.networkRestored)$('network').open=true;
  }
  $('preview').hidden=!state.preview;
  const connectionVerified=!!current.loaderReady&&!serverOffline&&!freshRun;
  const connectionFinished=['start-done','check-loader-done','find-addresses-done','select-host-done'].includes(current.phase);
  const installationComplete=!!current.installed&&!connectionFinished&&!freshRun&&!current.busy&&!current.error;
  const connectionPending=current.busy&&['start','check-loader'].includes(current.phase);
  const primaryLoaded=connectionVerified;
  text('connect-title','Connect and install the save');
  const windows=current.hostPlatform==='win32';
  $('connection-preflight').hidden=windows||primaryLoaded||connectionPending;
  const connectionStatus=serverOffline?'Waiting for the local server…':
   connectionPending?'Connecting to the console…':
   connectionVerified?'Console connection verified.':
   !freshRun&&connectionFinished?'The previous connection result is saved. Connect again if the console was restarted or the loader was removed.':'';
  $('connection-reset').hidden=!(current.loaderReady||connectionFinished||current.installed);
  $('connection-reset').disabled=locked;
  $('connect-state').hidden=!connectionStatus;
  text('connect-state',connectionStatus);
  $('connect-help').hidden=!!connectionStatus;
  text('connect-help',!current.sdReady?'Copy the SD archive and confirm it in step 1 first.':!current.prepared?'Complete PC setup in step 2 before connecting.':'If detection fails, open Console IP below Session status.');
  $('save-instructions').hidden=!connectionVerified&&!current.busy;
  $('installed-next').hidden=!installationComplete;$('installed-next').disabled=locked;
  const link=getLink(),ftp=getFtp();
  const online=!!link?.online&&!state.preview&&!serverOffline,filesOnline=online&&!!link.files;
  const ftpOnline=!!ftp?.online&&!ftp?.refreshError&&!state.preview&&!serverOffline;
  const connected=online||ftpOnline;
  if(connected&&!wasOnline&&!setupChosen){
   view='run';
   step=5;freshRun=false;
   document.getElementById('session-activity').open=false;
  }
  wasOnline=connected;
  $('flow').dataset.connected=String(connected&&view==='run');
  const saveUpdateRequired=view==='run'&&!!current.needsSaveUpdate;
  const nextLaunchContext=JSON.stringify([connected,saveUpdateRequired]);
  if(nextLaunchContext!==launchContext){
   $('launch-instructions').open=!connected;
   launchContext=nextLaunchContext;
  }
  $('launch-instructions').hidden=saveUpdateRequired;
  $('launch-guide').hidden=saveUpdateRequired;
  $('restart-help').hidden=saveUpdateRequired;
  text('launch-note',installationComplete?'Connection and save installation are complete. Open Load Game now.':view==='run'?'Open Load Game using the save already installed on the console.':'Install the save first, then follow these launch instructions.');
  $('restart-run').hidden=false;
  $('restart-run').disabled=working||serverOffline;
  $('previous-launcher').hidden=view==='run';
  $('connect-back').hidden=view==='run';
  $('windows').hidden=!windows||online||view==='run'||![2,3,4].includes(step);
  $('windows-refresh').disabled=locked;
  $('windows-report').disabled=!!state.preview||!current.windowsWifi?.checkedAt;
  $('windows-find').hidden=step!==3&&step!==4;
  $('windows-find').disabled=locked||!current.sdReady||connectionVerified;
  const wifi=current.windowsWifi||{},adapters=wifi.adapters||[];
  const wifiStatus={
   'inspection-failed':'Wi-Fi inspection failed. Retry the check; adapter compatibility is unknown.',
   'no-adapter':'Windows does not see a physical Wi-Fi adapter. Check that it is enabled and its driver is installed.',
   'adapter-unavailable':'Wi-Fi is disabled or its radio is off. Enable it in Windows and check again.',
   'service-unavailable':'Windows WLAN AutoConfig is not running. Start it in Windows Services and check again.',
   'ldn-unavailable':'Wi-Fi detected. LDN compatibility is not yet verified.'
  };
  text('windows-status',wifiStatus[wifi.status]||'Waiting for the Wi-Fi check…');
  $('windows').dataset.tone=['inspection-failed','no-adapter','adapter-unavailable','service-unavailable'].includes(wifi.status)?'error':'pending';
  text('windows-adapters',adapters.length?t('Detected Wi-Fi adapters: {names}').replace('{names}',adapters.map(a=>a.name+' ('+a.description+')').join(', ')):'');
  const nextWifiStamp=JSON.stringify(adapters)+t('Native Windows Wi-Fi available');
  if(nextWifiStamp!==wifiStamp){
   wifiStamp=nextWifiStamp;
   $('windows-checks').replaceChildren();
   for(const adapter of adapters){
   const details=document.createElement('details'),heading=document.createElement('summary');
   heading.textContent=adapter.name+' · '+t(adapter.compatibility?.ordinaryWifi?'Native Windows Wi-Fi available':'Native Windows Wi-Fi requires attention');details.append(heading);
   const monitor=adapter.monitor||{};
   const monitorText=monitor.status==='active'?'Monitor mode: currently enabled; reception not tested.':monitor.status==='advertised'?'Monitor mode: advertised by driver; reception not tested.':monitor.status==='unsupported'?'Monitor mode: not advertised by this driver.':
    monitor.reason==='administrator-required'?'Monitor mode: not checked; requires an elevated diagnostic.':
    monitor.reason==='capture-tool-missing'?'Monitor mode: unknown; Npcap diagnostic tool is not installed.':'Monitor mode: could not be checked.';
   const reasons={
    'adapter-disabled':'Adapter is disabled in Windows.',
    'wlan-service-unavailable':'Windows WLAN AutoConfig is not running.',
    'native-wlan-unavailable':'Windows WLAN API did not confirm access to this adapter.',
    'radio-off':'Wi-Fi radio is off.'
   };
   const lines=[adapter.description,[adapter.driverProvider,adapter.driverVersion,...(adapter.phyTypes||[])].filter(Boolean).join(' · '),
    ...(adapter.compatibility?.reasons||[]).map(reason=>reasons[reason]).filter(Boolean),monitorText,
    'Raw frame reception, transmission and LDN lobby connection: not verified.'];
   for(const value of lines.filter(Boolean)){const line=document.createElement('p');line.textContent=t(value);details.append(line);}
   $('windows-checks').append(details);
   }
  }
  const wifiError=wifi.error||wifi.inventoryError||'';
  text('windows-error',wifiError);$('windows-error').hidden=!wifiError;
  $('linux-checks').hidden=windows;
  $('linux-requirements').hidden=windows||view!=='setup'||step!==2;
  $('save-update').hidden=!saveUpdateRequired;
  text('save-update','Installation of a Civilization VII save for this IZ2S build has not been confirmed yet. Start the full procedure to update the SD files and install the matching save.');
  text('link-state',state.preview?'Preview · device commands disabled':serverOffline?'Waiting for the local server…':connected?'The console is available. No need to repeat the launch steps.':'If IZ2S is already running, connect PC Link or FTP below. Otherwise, follow the launch instructions.');
  $('services').dataset.connected=String(connected);
  text('services-state',online&&ftpOnline?'PC Link + FTP connected':online?'PC Link connected':ftpOnline?'FTP connected':'No active connection');
  const linkControlsLocked=state.preview||serverOffline||resettingSession;
  const pcPaused=link?.enabled===false;
  const pcStatus=state.preview?'Preview · device commands disabled':serverOffline||link?.error?'Status unavailable':pcPaused?'Paused on this PC':online?'Connected to this panel':link===undefined?'Checking connection…':link===null?'Not configured':'Waiting for console';
  $('pc-card').dataset.online=String(online);
  text('pc-status',t(pcStatus)+(online&&link.consoleAddress?' · '+link.consoleAddress:''));
  text('pc-toggle',pcPaused?'Resume PC Link':'Pause PC Link');
  $('pc-toggle').hidden=!link?.service;
  $('pc-toggle').disabled=linkControlsLocked||!link?.service||link.fileTransfers||!!link.queued;
  const ftpStatus=state.preview?'Preview · device commands disabled':serverOffline||ftp?.refreshError?'Status unavailable':ftpOnline?'Connected to this panel':!ftp?'Checking connection…':ftp.enabled?'Connection lost':'Not connected to this panel';
  $('ftp-card').dataset.online=String(ftpOnline);
  text('ftp-status',t(ftpStatus)+(ftpOnline&&ftp.host?' · '+ftp.host:''));
  text('ftp-help',ftpOnline?'File access through FTP is ready.':ftp?.refreshError||serverOffline?'Refresh the status to check the connection.':'If FTP is already enabled on the console, click Connect FTP here.');
  text('ftp-open',ftp?.enabled?'Reconnect FTP':'Connect FTP');
  $('ftp-open').hidden=ftpOnline;
  $('ftp-close').hidden=!ftp?.enabled;
  $('ftp-open').disabled=$('ftp-close').disabled=linkControlsLocked||!!ftp?.busy;
  $('pc-link-guide').hidden=online||serverOffline||pcPaused;
  $('ftp-guide').hidden=ftpOnline||serverOffline;
  $('link-error').hidden=!link?.error;text('link-error',link?.error||'');
  text('files',filesOnline?'Super File Bros':'Super File Bros. · FTP');
  $('files').hidden=!filesOnline&&!ftpOnline;
  $('library').disabled=!online;
  $('files').disabled=$('link-refresh').disabled=state.preview||serverOffline||resettingSession;
  $('reset-session').disabled=locked||!!link?.fileTransfers;
  text('reset-session',resettingSession?'Resetting session…':'Reset session');

  $('flow').dataset.view=view;
  $('steps').hidden=view==='run';
  text('step-3','3. Connection and save');
  text('step-5','4. Launch IZ2S');
  $('network').hidden=view!=='setup'||![3,4].includes(step);
  const addresses=current.addressOptions||[],nextAddresses=JSON.stringify([addresses,t('Network candidate'),current.selectedHost]);
  if(addressStamp!==nextAddresses){
   addressStamp=nextAddresses;const previous=$('host-options').value;$('host-options').replaceChildren();
   const placeholder=document.createElement('option');placeholder.value='';placeholder.textContent=t(addresses.length?'Choose an address':'No suggested addresses yet');$('host-options').append(placeholder);
   for(const item of addresses){const option=document.createElement('option');option.value=item.address;option.textContent=item.address+' · '+t(item.verified?'Loader responded':'Network candidate');$('host-options').append(option);}
   $('host-options').value=addresses.some(item=>item.address===previous)?previous:addresses.some(item=>item.address===current.selectedHost)?current.selectedHost:'';
  }
  text('target-mode',current.selectedHost?t('Selected console:')+' '+current.selectedHost:'Automatic console detection is enabled.');
  for(const mode of ['setup','run']){$('view-'+mode).setAttribute('aria-pressed',String(view===mode));$('view-'+mode).classList.toggle('selected',view===mode);}
  // Internal steps 3 and 4 share one visible connection-and-save stage.
  for(let i=1;i<=5;i++)$('stage-'+i).hidden=[3,4].includes(i)?![3,4].includes(step):step!==i;
  for(const i of [1,2,3,5]){
   const selected=i===(step===4?3:step);
   $('step-'+i).parentElement.hidden=false;
   $('step-'+i).setAttribute('aria-current',selected?'step':'false');
   $('step-'+i).classList.toggle('selected',selected);
   $('step-'+i).parentElement.classList.toggle('current',selected);
   $('step-'+i).disabled=working;
  }
  $('step-5').disabled=working||(view!=='run'&&!state.preview&&!online&&!installationComplete);
  $('view-setup').disabled=$('view-run').disabled=working;
  for(const id of ['pc-action','keys-replace','refresh','keys','radio'])$(id).disabled=locked;
  for(const id of ['find-addresses','auto-host','host-options','manual-host'])$(id).disabled=locked;
  $('select-host').disabled=locked||!$('host-options').value;
  $('manual-check').disabled=locked||!$('manual-host').value.trim();
  $('download-sd').disabled=downloading||isUnavailable();
  $('confirm-sd').disabled=working||serverOffline||!$('sd-copied').checked;
  $('start').disabled=windows||locked||primaryLoaded||!current.prepared||!current.sdReady||!$('lobby').checked;
  for(const id of ['lobby','sd-copied'])$(id).disabled=working||serverOffline;
  $('install').disabled=locked||!connectionVerified||current.networkRecoveryAvailable||!current.sdReady;
  const installing=current.busy&&current.phase==='install';
  text('save-action-title',installing?'Installation is in progress.':installationComplete?'Launch save installed.':!connectionVerified?'Connect the console':'Install the launch save');
  text('save-action-help',installing?'Wait for completion. Progress is shown in Session status above.':installationComplete?'Continue on the console using the launch instructions below.':!current.sdReady?'Copy the SD archive and confirm it in step 1 first.':!connectionVerified?'The launch save will be installed automatically after the connection is verified.':'The connection is verified. Install the launch save or retry if the transfer failed.');
  text('install',installing?'Installing…':current.error?'Retry save installation':'Install launch save');
  $('install').hidden=working||installationComplete||!connectionVerified;
  $('stage-4').hidden=view!=='setup'||![3,4].includes(step)||(!connectionVerified&&!installing);
  $('stage-3').hidden=view!=='setup'||![3,4].includes(step)||installing||connectionVerified;
  if(view==='run'){$('stage-5').hidden=false;step=5;}
  $('services').hidden=view!=='run'&&step!==5;
  $('flow').dataset.step=String(step===4?3:step===5?4:step);

  $('stop').hidden=!current.busy;$('stop').disabled=posting||isUnavailable();
  const error=actionError||current.error||'';
  $('result').hidden=!current.busy&&!error&&!current.networkRecoveryAvailable&&!(step>=3&&step<=4);
  const recovering=current.networkRecoveryAvailable&&!current.busy;
  const stages={preparing:'Preparing Wi-Fi for the console…',searching:'Finding and joining the wireless lobby…',sending:'Sending the initial loader…',restoring:'Restoring PC Wi-Fi…',restored:'PC Wi-Fi restored. Checking the console…',checking:'Checking the console connection…',ready:'Console connection verified.'};
  const stepHelp={1:'Extract the archive to the SD card, then confirm the copy below.',2:'Complete the three PC checks below. IZ2S Platform offers the next action.',3:'Follow the connection steps below.',4:'Keep the connection screen open while the launch save is installed.',5:'Continue on the console using the launch instructions below.'};
  let status=current.busy?(current.phase==='start'?stages[current.networkStage]||phases.start:phases[current.phase]||'Please wait…'):error?'Operation failed.':recovering?'Wi-Fi recovery is needed.':current.phase==='install-done'&&step===5?phases['install-done']:current.phase==='launch-done'?phases['launch-done']:connectionVerified&&step>=3&&step<=4?'Console connection verified.':'Ready for the next step.';
  if(current.busy&&current.phase==='install')status=current.operationProgress?.stage==='writing'?'Writing the launch save on the console…':current.operationProgress?t('Transferring the launch save: {percent}%').replace('{percent}',current.operationProgress.percent):'Preparing the launch save…';
  if(!current.busy&&!error&&!recovering&&step===4&&connectionVerified)status=installationComplete?'Launch save installed.': 'Ready to install the launch save.';
  if(!current.busy&&!error&&!recovering&&step===3&&primaryLoaded)status='Game loader found. Ready to install the launch save.';
  text('stop',current.phase==='start'||current.phase==='restore-network'?'Stop and restore Wi-Fi':'Stop');
  if(isUnavailable())status='Waiting for the local server…';
  text('status',status);
  text('state-label',isUnavailable()?'Offline':error||recovering?'Needs attention':current.busy?'In progress':step===4&&connectionVerified&&!installationComplete?'Ready':'Ready');
  $('result').dataset.tone=error||recovering?'error':current.busy?'busy':'ready';
  let help=current.busy?(current.phase==='start'&&current.networkStage==='restoring'?'Wait for the saved Wi-Fi connection and its IP address. IZ2S Platform checks the console only after recovery.':'Keep the console screen open. You can follow the details in the session log.'):recovering?'Restore the saved PC Wi-Fi connection before continuing.':error?'Check the console screen and the session log before retrying.':stepHelp[step];
  if(!current.busy&&!error&&!recovering&&step===4&&connectionVerified)help=installationComplete?'Continue on the console using the launch instructions below.':'Keep Awaiting payload open. Do not restart the console. Use the save installation button only to resume an interrupted transfer.';
  if(!current.busy&&!error&&!recovering&&step===3&&primaryLoaded)help='Keep Awaiting payload open. Do not restart the console. Use the save installation button only to resume an interrupted transfer.';
  if(!current.busy&&!error&&!recovering&&step===4&&!connectionVerified)help=stepHelp[3];
  text('status-help',help);
  const firstErrorLine=error.split('\n').find(line=>line.trim());
  text('error',error.length>350?[t('Technical details are in Full output.'),firstErrorLine?.slice(0,240)].filter(Boolean).join(' '):error);
  $('error').hidden=!error;
  text('address',current.host?t(connectionVerified?'Verified console:':'Last known console:')+' '+current.host:'');$('address').hidden=!current.host;
  $('restore-network').hidden=!recovering;$('restore-network').disabled=locked;
  $('progress').hidden=!current.busy;
  if(current.busy&&current.phase==='start'&&current.networkStage==='sending'&&current.progress?.total){$('progress').value=Math.round(100*current.progress.line/current.progress.total);}
  else if(current.busy&&current.phase==='install'&&current.operationProgress?.stage==='transferring')$('progress').value=current.operationProgress.percent;
  else $('progress').removeAttribute('value');
  const nextAction=globalThis.IZ2S_IZ2SCIV.pcAction(current),radios=setup.radios||[];
  const wifiReady=setup.capabilitiesReady&&setup.networkManager&&radios.includes(current.phy);
  const readyCount=[setup.keysReady,setup.dependenciesReady,wifiReady].filter(Boolean).length;
  const pcLabels={keys:'Choose prod.keys',prepare:'Prepare PC',permissions:'Allow Wi-Fi access',refresh:'Check again',radio:'Choose Wi-Fi adapter',continue:'Continue to connection',unsupported:'Linux is required'};
  const pcHelp={keys:'Choose your prod.keys. IZ2S Platform will validate it before saving.',prepare:'The components will be installed automatically. This may take a few minutes.',permissions:'The next action opens a Linux administrator prompt for the network helper.',refresh:!setup.networkManager?'Start NetworkManager on this PC, then check again.':'Connect a Wi-Fi adapter to this PC, then check again.',radio:'Choose the adapter that will connect to the console lobby.',continue:'PC setup is complete. Continue opens step 3 only. Nothing is sent to the console until you confirm the fresh lobby and click Connect and install save.',unsupported:'First setup requires native Linux. Open IZ2S Platform on a Linux PC with Wi-Fi.'};
  text('pc-state',keyLoading?'Checking prod.keys…':current.busy?(phases[current.phase]||'Please wait…'):t('{count} of 3 ready').replace('{count}',readyCount));
  if(windows)text('pc-state','Windows · Civ7 network mode');
  $('pc-state').dataset.ready=String(nextAction==='continue');
  text('pc-action',keyLoading?'Checking prod.keys…':current.busy?'Please wait…':pcLabels[nextAction]);
  $('pc-action').disabled=state.preview?serverOffline:locked||nextAction==='unsupported';
  if(state.preview)text('pc-action','Continue to connection');
  text('pc-help',windows?'Continue to find a loader already open on the console. No Linux tools or prod.keys are needed for this network stage.':pcHelp[nextAction]);
  for(const [id,ready] of [['keys',setup.keysReady],['tools',setup.dependenciesReady],['wifi',wifiReady]]){
   $('check-'+id).dataset.ready=String(!!ready);
   text(id+'-state',ready?(id==='keys'?'Saved and verified':'Ready'):'Needs setup');
  }
  text('keys-file',setup.keysReady?(keyName||'prod.keys'):'No file added yet.');
  $('keys-replace').hidden=!setup.keysReady;
  text('keys-feedback',keyError?(t('File was not imported.')+' '+t(keyError)+(setup.keysReady?' '+t('The previously saved file is still in use.') : '')):keyLoading?'Checking prod.keys…':keyImported?'File saved and verified. You do not need to select it again.':'');
  $('keys-feedback').hidden=!keyError&&!keyLoading&&!keyImported;
  $('keys-feedback').dataset.error=String(!!keyError);
  $('keys-feedback').setAttribute('role',keyError?'alert':'status');
  text('radio-state',radios.length===1?t('Wi-Fi adapter: {name} · selected automatically').replace('{name}',radios[0]):radios.length?'Select a Wi-Fi adapter below.':'No Wi-Fi radio found');
  $('radio-field').hidden=!radios.length||(radios.length===1&&radios[0]===current.phy);
  text('system-state','Linux: '+t(setup.supported?'Ready':'Required')+' · NetworkManager: '+t(setup.networkManager?'Ready':'Required'));
  const nextRadio=JSON.stringify([radios,current.phy,t('Choose Wi-Fi adapter')]);
  if(radioStamp!==nextRadio){radioStamp=nextRadio;$('radio').replaceChildren();
   if(radios.length>1){const option=document.createElement('option');option.value='';option.textContent=t('Choose Wi-Fi adapter');$('radio').append(option);}
   for(const value of radios.length?radios:['']){const option=document.createElement('option');option.value=value;option.textContent=value||t('No Wi-Fi radio found');$('radio').append(option);}
   $('radio').value=current.phy||(radios.length===1?radios[0]:'')||'';
  }
  if(wasInitialized&&previousStep!==step)focusStep();
 }
 function activityLog(state,full){
  const entries=state.log||[];if(full)return entries;
  const result=[];
  for(const entry of entries){
   let message;
   const phase=/^IZ2SCiv: (.+)$/.exec(entry.message)?.[1];
   const progress=/^Sending payload: (\d+)%/.exec(entry.message),host=/^Game loader found at ([\d.]+):/.exec(entry.message);
   if(progress){
    const row={...entry,message:t('Sending initial loader: {percent}%').replace('{percent}',progress[1]),progress:true};
    if(result.at(-1)?.progress)result[result.length-1]=row;else result.push(row);
    continue;
   }
   if(phase&&phases[phase])message=t(phases[phase]);
   else if(entry.message===state.iz2sciv?.error||/^Error:|^LDN delivery stopped:/.test(entry.message))message=entry.message.split('\n')[0];
   else if(host)message=t('Game loader found: {address}').replace('{address}',host[1]);
   else if(entry.message.startsWith('Remote JS Loader verified'))message=t('Game loader connection verified.');
   else if(entry.message.startsWith('[net] Restoring'))message=t('Restoring PC Wi-Fi…');
   else if(entry.message==='CIV7_LDN_NETWORK_RESTORED')message=t('PC Wi-Fi restored.');
   else if(entry.message==='Lobby joined.')message=t('Console lobby joined.');
   else if(entry.message==='CIVSWITCH_LOADER_TIMEOUT')message=t('Game loader did not respond. See Full output for details.');
   else if(entry.message==='=== IZ2S CORE IS RUNNING ===')message=t('IZ2S core started. Follow the console steps to open the launcher.');
   if(message)result.push({...entry,message});
  }
  return result;
 }
 return {render,activityLog};
}};
