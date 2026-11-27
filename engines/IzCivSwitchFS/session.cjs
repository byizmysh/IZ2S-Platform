'use strict';
const path = require('node:path');
const fs = require('node:fs/promises');
const {spawn} = require('node:child_process');
const {readJson, saveJson, redact, createLedger} = require('../../core/session/ledger.cjs');
const {randomUUID,createHash} = require('node:crypto');
const {compatibilityContract} = require('./src/runtime-policy.cjs');
const integration = require('../CivSwitchLDN/integration.cjs');
const discovery = require('../CivSwitch/runtime/discovery.cjs');
const SAVE_NAME='IZ2SCIV.Civ7Save';
const {privateIP}=require('../../core/session/network-discovery.cjs');

function create(options) {
  const {root, preview = false, launch = spawn, platform = process.platform} = options;
  const wifi=options.windowsWifi||require('../../core/platform/windows-wifi.cjs');
  let windowsWifi={adapters:[],recoveryAvailable:false,ldnSupported:false};
  let wifiCheckedAt=0,wifiChecking=null;
  async function refreshWifi(force=false){
    if(platform!=='win32'||preview)return;
    if(wifiChecking)return wifiChecking;
    if(!force&&Date.now()-wifiCheckedAt<60000)return;
    wifiChecking=wifi.inspect(root,{platform}).then(value=>{windowsWifi=value;}).catch(error=>{
      windowsWifi={adapters:[],selectedId:'',ldnSupported:false,status:'inspection-failed',checkedAt:new Date().toISOString(),error:String(error.message)};
    }).finally(()=>{wifiCheckedAt=Date.now();wifiChecking=null;});
    return wifiChecking;
  }

  // LDN is only the initial delivery transport. Its legacy save installer and
  // native launcher wrapper must never participate in the IZ2SCiv chain.
  let addressOptions=[],foundHosts=[];
  const discoverNetwork=async args=>{
    foundHosts=await (options.discoverNetwork||discovery.discover)(args);
    await refreshAddresses();return foundHosts;
  };
  const ldn = options.ldn || require('../CivSwitchLDN/session.cjs').create({...options,discoverNetwork});
  const folder = path.join(root, 'engines/IzCivSwitchFS');
  const file = path.join(root, 'data/state/iz2sciv.json');
  const ledger=createLedger(root);
  let saved = {phase:'idle', sdReady:false, installed:false, host:''};
  let task = null, child = null, posting = false, interrupted = false, signal;
  const busy = () => posting || !!task;
  let logQueue=Promise.resolve(),lastAttempt=null;
  const events = [];
  let operationProgress=null;
  const loaded = readJson(file, {}).then(async value => {
    saved = {...saved,...value};
    if (saved.busy) {
      saved.phase = 'interrupted'; saved.error = 'Previous operation interrupted. Check the console before retrying.';
    }
    saved.busy = false;
    if(/^[a-f0-9-]{36}$/.test(saved.id||'')){
      const previous=await ledger.read(saved.id).catch(()=>null);
      if(previous?.loaderId==='iz2sciv-core'){
        lastAttempt=previous;
        events.push(...await ledger.recent(saved.id).catch(()=>[]));
      }
    }
  });
  const persist = () => saveJson(file, saved);
  const runtimeIdentity = async () => {
    const bytes=await fs.readFile(path.join(root,'engines/iz2s-core/runtime/package-manifest.json'));
    const boot=JSON.parse(await fs.readFile(path.join(folder,'boot-revision.json'),'utf8'));
    if(!/^[a-z0-9][a-z0-9.-]{0,63}$/.test(boot.revision||''))throw Error('Invalid launch save revision');
    const hash=value=>createHash('sha256').update(value).digest('hex');
    return {runtimeFingerprint:hash(bytes),
      saveBootRevision:boot.revision,
      saveCompatibilityFingerprint:hash(JSON.stringify(compatibilityContract(JSON.parse(bytes))))};
  };
  async function refreshAddresses(){
    const known=discovery.knownHosts(options.connections?.snapshot?.(),saved.host);
    addressOptions=await (options.addressOptions||require('./tools/address-options.cjs').list)({known,found:foundHosts});
  }
  const log = message => {
    const template=/^Template (\d+)\/(\d+) received\./.exec(String(message));
    if(saved.phase==='install'&&template)operationProgress={stage:Number(template[1])===Number(template[2])?'writing':'transferring',percent:Math.round(Number(template[1])*100/Number(template[2]))};
    events.push({at:new Date().toISOString(),type:'iz2sciv',message:redact(String(message)).slice(-4000)});
    if(events.length > 250) events.shift();
    if(saved.id){const id=saved.id;logQueue=logQueue.then(()=>ledger.append(id,'iz2sciv',message)).catch(()=>{});}
  };
  const assertRunning = () => {if(interrupted) throw Error('Operation stopped. Check the console before retrying.');};
  function run(program,args,marker) {
    assertRunning();
    return new Promise((resolve,reject) => {
      let output='', pending='', done=false, timedOut=false;
      const processHandle=launch(program,args,{cwd:folder,stdio:['ignore','pipe','pipe'],env:{...process.env,...options.configuration?.env}});
      child=processHandle;
      const timeout=setTimeout(()=>{timedOut=true;processHandle.kill('SIGTERM');},10*60*1000);
      const finish=error=>{if(done)return;done=true;clearTimeout(timeout);if(child===processHandle)child=null;error?reject(error):resolve(output);};
      for(const stream of [processHandle.stdout,processHandle.stderr])stream.on('data',chunk=>{
        output=(output+chunk).slice(-1024*1024);pending+=chunk;
        const lines=pending.split(/[\r\n]+/);pending=lines.pop().slice(-4000);lines.filter(Boolean).forEach(log);
      });
      processHandle.once('error',finish);
      processHandle.once('close',(code)=>{
        if(pending)log(pending);
        finish(interrupted?Error('Operation stopped. Check the console before retrying.'):
          timedOut?Error('Operation timed out. Check the console before retrying.'):
          code!==0?Error(output.slice(-1600)||'Operation failed: '+code):
          marker&&!output.includes(marker)?Error('Console completion was not confirmed. '+output.slice(-800)):null);
      });
    });
  }
  async function transport(action,extra={}) {
    assertRunning();
    const started=new Date().toISOString();
    await ldn.action({action,...extra});
    await ldn.waitForOperation();
    const transportSnapshot=await ldn.snapshot(),state=transportSnapshot.civswitchLdn;
    // Keep transport details in the same downloadable attempt as the user action.
    for(const entry of transportSnapshot.log||[]){
      if(entry.at>=started){const id=saved.id;logQueue=logQueue.then(()=>ledger.append(id,entry.type,entry.message,{at:entry.at})).catch(()=>{});}
    }
    assertRunning();
    if(state.error)throw Error(state.error);
    return state;
  }
  async function loader() {
    const state=await transport('check-loader',saved.selectedHost?{automatic:false,host:saved.selectedHost}:{automatic:true});
    if(!state.loaderReady||!state.suggestedHost)throw Error('Game loader not found. Open Awaiting payload on the console.');
    saved.host=state.suggestedHost;return saved.host;
  }
  async function installSave() {
    // Recheck the protocol after Wi-Fi recovery; a remembered address is not
    // proof that this console is still accepting a payload.
    const host=await loader();assertRunning();
    const identity=await runtimeIdentity();assertRunning();
    saved.phase='install';saved.installed=false;
    operationProgress={stage:'preparing',percent:0};
    await persist();assertRunning();
    await run(process.execPath,[path.join(folder,'tools/persist-install.mjs'),'--chain',host],
      'Installed. Restart the game, then open Load Game menu.');
    assertRunning();
    const currentIdentity=await runtimeIdentity();
    if(currentIdentity.runtimeFingerprint!==identity.runtimeFingerprint)throw Error('The runtime changed during installation. Retry with the current SD package.');
    if(currentIdentity.saveBootRevision!==identity.saveBootRevision)throw Error('The launch save revision changed during installation. Retry with the current panel.');
    assertRunning();saved.installed=true;saved.saveName=SAVE_NAME;Object.assign(saved,identity);
  }
  async function ftpHost() {
    if(options.findFtp)return options.findFtp({signal:signal.signal});
    const hint=await require('../iz2s-core/tools/ftp-connect.cjs').credentials();
    const hosts=await discovery.discover({known:[hint.host,saved.host],signal:signal.signal,
      test:require('./tools/ftp-probe.cjs').probe});
    if(hosts.length!==1)throw Error(hosts.length?'Several FTP consoles found. Leave FTP open on one console.':'IZ2S FTP not found. Open IZ2s and enable FTP.');
    return hosts[0];
  }
  async function action(body={}) {
    if(posting||task)throw Error('Wait for the current operation to finish');
    posting=true;
    try {
      await loaded;
      if(preview)throw Error('Preview cannot send commands or change console files.');
      const command=body.action;
      if(!['reset-connection','refresh','import-keys','configure','prepare','permissions','confirm-sd','start','check-loader','install','launch','check-launcher','find-addresses','select-host','auto-host','restore-network','wifi-status','wifi-restart'].includes(command))throw Error('Unknown IZ2SCiv action');
      if(command==='wifi-status'){await refreshWifi(true);return {ok:true};}
      if(command==='wifi-restart'&&platform!=='win32')throw Error('Windows Wi-Fi control is only available on Windows.');
      if(platform==='win32'&&['start','prepare','permissions'].includes(command))throw Error('Native Windows LDN delivery is not available. Restarting Wi-Fi does not install the loader. Open Awaiting payload using a supported initial loader first.');
      if(command==='reset-connection'){
        // Forget a previous console result without touching the console or PC setup.
        await ldn.action({action:'discard'});
        saved.phase='connection-reset';saved.installed=false;saved.selectedHost='';
        delete saved.error;delete saved.lastAction;
        foundHosts=[];addressOptions=[];operationProgress=null;
        await persist();return {ok:true};
      }
      if(command==='auto-host'){saved.selectedHost='';saved.phase='auto-host-done';delete saved.error;await persist();return {ok:true};}
      if(command==='select-host'&&!privateIP(body.host))throw Error('Choose a private local IPv4 address.');
      if(command==='confirm-sd'){
        if(body.copied!==true)throw Error('Confirm that the ZIP was extracted to the SD card root.');
        saved.sdReady=true;saved.sdPreparation='manual-zip';saved.phase='confirm-sd-done';delete saved.error;
        await persist();return {ok:true};
      }
      if(command==='import-keys')return await ldn.action({action:command,content:body.content});
      if(command==='configure')return await ldn.action({action:command,phy:body.phy});
      if(command==='refresh'){await Promise.all([ldn.refresh(true),refreshWifi(true)]);return {ok:true};}
      if(command==='start'&&body.confirm!==true)throw Error('Confirm a fresh Civilization VII wireless lobby before starting.');
      if(command==='start'&&body.installAfterSuccess!==true)throw Error('Use Connect and install save to complete the connection.');
      if(command==='install'&&body.replaceTrigger!==true)throw Error('Confirm installation of IZ2SCIV.Civ7Save before installing.');
      if(command==='launch'&&body.freshBoot!==true)throw Error('Confirm a fresh console boot before launching IZ2S.');
      if(['start','install','launch'].includes(command)&&!saved.sdReady)throw Error('Copy the SD archive and confirm it in step 1 first.');
      interrupted=false;operationProgress=null;signal=new AbortController();saved.busy=true;saved.phase=command;saved.lastAction=command;delete saved.error;
      if(['select-host','find-addresses'].includes(command)){saved.selectedHost='';foundHosts=[];addressOptions=[];}
      saved.id=randomUUID();events.length=0;
      const attempt={id:saved.id,upstream:true,loaderId:'iz2sciv-core',kind:'iz2sciv',status:'starting',created:new Date().toISOString()};
      if(['start','install'].includes(command))saved.installed=false;
      await ledger.write(attempt);
      await persist();
      task=Promise.resolve().then(async()=>{
        if(command==='wifi-restart'){
          const args=wifi.args(root,body.adapter,body.recover===true);
          await run('powershell.exe',args,'IZ2S_WINDOWS_WIFI_RESTORED');
          windowsWifi=await wifi.inspect(root,{platform});
        } else if(command==='select-host'){
          const state=await transport('check-loader',{automatic:false,host:body.host});
          if(!state.loaderReady||state.suggestedHost!==body.host)throw Error('The selected address did not confirm the game loader.');
          saved.selectedHost=body.host;saved.host=body.host;foundHosts=[body.host];await refreshAddresses();
        } else if(command==='find-addresses'){
          // Refresh hints even when no loader responds; display them as unverified.
          try{await loader();}finally{await refreshAddresses();}
        } else if(command==='prepare')await transport('prepare');
        else if(command==='permissions'){
          const state=(await ldn.snapshot()).civswitchLdn;
          if(!state.setup?.supported||!state.setup.dependenciesReady)throw Error('Prepare PC dependencies first.');
          // Restrict elevated permissions to the dedicated copy, never system Python.
          const helper=integration.paths(root).helper;
          const stat=await fs.lstat(helper);
          if(!stat.isFile()||stat.isSymbolicLink())throw Error('The dedicated network helper must be a regular file.');
          await run('pkexec',['setcap','cap_net_admin,cap_net_raw=ep',helper]);
          await ldn.refresh(true);
          if(!(await ldn.snapshot()).civswitchLdn.setup.capabilitiesReady)throw Error('Network permissions were not granted.');
        } else if(command==='check-launcher'){
          const host=await ftpHost();assertRunning();
          await run(options.python||require('../../core/platform/python.cjs').command(root,{platform}),[path.join(folder,'tools/prepare-sd.py'),host,'--verify-only'],'IZ2SCIV_SD_VERIFIED');
          saved.host=host;saved.sdReady=true;
        } else if(command==='restore-network'){
          await run('bash',[path.join(root,'engines/CivSwitchLDN/bash/network-panel.sh'),'--restore',integration.paths(root).helper],'CIV7_LDN_NETWORK_RESTORED');
        } else if(command==='start') {
          const state=await transport('start',{confirm:true,freshLobby:true,...(saved.selectedHost?{automatic:false,host:saved.selectedHost}:{automatic:true}),installAfterSuccess:false});
          if(!state.loaderReady)throw Error('Game loader was not confirmed.');
          saved.host=state.suggestedHost;
          assertRunning();
          log('Connection verified. Installing IZ2SCIV.Civ7Save automatically.');
          await installSave();
        } else if(command==='check-loader')await loader();
        else if(command==='install'){
          await installSave();
        } else if(command==='launch'){
          const host=await loader();assertRunning();
          await run(process.execPath,[path.join(folder,'tools/send.mjs'),host],'=== IZ2S CORE IS RUNNING ===');
        }
        assertRunning();saved.phase=command==='start'?'install-done':command+'-done';attempt.status='complete';log('IZ2SCiv: '+saved.phase);
      }).catch(async error=>{saved.phase=interrupted?'interrupted':'failed';saved.error=redact(error.message);attempt.status=interrupted?'cancelled':'failed';attempt.error=saved.error;log(saved.error);
        if(!interrupted&&['start','check-loader','install','launch','find-addresses','select-host'].includes(command))await refreshAddresses().catch(()=>{});
      })
        .finally(async()=>{saved.busy=false;try{await logQueue;await ledger.write(attempt);lastAttempt=attempt;await persist();}finally{task=null;signal=null;}});
      return {ok:true,started:true};
    } finally {posting=false;}
  }
  async function stop(options={}) {
    interrupted=true;signal?.abort();child?.kill('SIGTERM');
    await ldn.stop();await task;await options.beforeSignal?.();return {ok:true};
  }
  async function snapshot() {
    await loaded;
    await refreshWifi();
    const state=await ldn.snapshot(), transport=state.civswitchLdn;
    const identity=await runtimeIdentity();
    const matchingSave=saved.saveName===SAVE_NAME&&saved.saveCompatibilityFingerprint===identity.saveCompatibilityFingerprint&&saved.saveBootRevision===identity.saveBootRevision;
    return {...state,civswitchLdn:undefined,connected:false,hardStopAvailable:false,
      active:busy()?{id:saved.id,kind:'iz2sciv',status:'starting'}:null,lastAttempt:lastAttempt||state.lastAttempt,
      log:[...state.log,...events].sort((a,b)=>a.at.localeCompare(b.at)),
      iz2sciv:{...saved,hostPlatform:platform,windowsWifi,installed:!!saved.installed&&matchingSave,needsSaveUpdate:!!saved.installed&&!matchingSave,
        saveName:SAVE_NAME,expectedSaveBootRevision:identity.saveBootRevision,operationProgress,networkRecoveryAvailable:await fs.access(path.join(root,'data/state/civ7-network.json')).then(()=>true,()=>false),addressOptions,busy:busy(),setup:transport.setup,prepared:transport.prepared,phy:transport.phy,
        loaderReady:transport.loaderReady,networkStage:transport.networkStage,networkRestored:transport.networkRestored,transportPhase:transport.phase,progress:{line:transport.index,total:transport.total}},
      connections:{...state.connections,locked:busy()},
    };
  }
  return {...ldn,action,setupAction:action,snapshot,stop,hardStop:stop,
    waitForOperation:()=>task||Promise.resolve(),ownsListener:busy,
    configureConnection:async body=>{if(task||posting)throw Error('Stop the current operation first');return ldn.configureConnection(body);},
    begin:()=>{throw Error('Use the IZ2SCiv core setup steps.');},
    setupReport:async()=>JSON.stringify((await snapshot()).iz2sciv),
  };
}
module.exports={create};
