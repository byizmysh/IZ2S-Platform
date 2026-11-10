'use strict';
const path = require('node:path'), net = require('node:net'), crypto = require('node:crypto');
const {spawn,execFile} = require('node:child_process');
const {readJson, saveJson, redact, createLedger} = require('../../core/session/ledger.cjs');
const {payload, sourcePlan, automaticPlan} = require('./input.cjs');
const discovery = require('./runtime/discovery.cjs');
const PORT = require('./engine.json').consolePort;
const loaderNotRunning='CivSwitch Remote JS Loader is not accepting connections on TCP 9026. Start the loader in the game, then check again.';
function probeError(message){
  if(message.includes('CIVSWITCH_LOADER_NOT_RUNNING')||message.includes('ConnectionRefusedError'))return loaderNotRunning;
  if(message.includes('CIVSWITCH_LOADER_TIMEOUT'))return 'No response from CivSwitch on TCP 9026. Check the game loader and local network.';
  if(message.includes('CIVSWITCH_LOADER_NETWORK_ERROR'))return 'The local network cannot reach the CivSwitch loader. Check the console address and LAN cable.';
  return message;
}
function localIPv4(host) {
  if (!net.isIPv4(host)) return false;
  const [a,b] = host.split('.').map(Number);
  return a === 10 || (a === 172 && b >= 16 && b <= 31) || (a === 192 && b === 168);
}
function pingNetwork(host) {
  return new Promise(resolve=>execFile('ping',['-n','-c','1','-W','1',host],{timeout:2000},error=>resolve(!error)));
}
function create({root, settings, arm, connections, configuration, preview = false,
  discoverNetwork=discovery.discover, launch = spawn, networkProbe = pingNetwork, pause = ms=>new Promise(resolve=>setTimeout(resolve,ms)), now = Date.now, loaderWaitMs = 120000, routeLookup = require('../../core/platform/network.cjs').routeTo,
  python = require('../../core/platform/python.cjs').command(root)}) {
  const folder = path.join(root, 'engines/CivSwitch'), file = path.join(root, 'data/state/civswitch.json');
  let saved = {phase:'idle',index:0,host:'',launcher:'izlauncher'}, task = null, child = null, jobId = null,
    interrupted = false, ready = false, verifiedHost = null, info = null, inputStatus = null, checkedAt = 0, checking = null,
    networkHost = '', networkReachable = false, routeError = '', actionBusy = false;
  const events = [],ledger=createLedger(root);let attempt=null,lastAttempt=null,logQueue=Promise.resolve();
  let restoredAttempt=false, search=null;
  const loaded = readJson(file, saved).then(value => {
    saved = {...saved,...value};
    if(saved.phase==='checking'){saved.phase=saved.phaseBeforeCheck||(saved.index===87?'entered':'idle');delete saved.phaseBeforeCheck;}
    if (['typing','auto-typing','installing','sending','recovery-clearing'].includes(saved.phase)) saved.phase = 'uncertain';
    // Old versions treated a harmless refused probe like uncertain HID input.
    if(saved.phase==='uncertain'&&!saved.pending&&saved.error?.includes('/probe.py')){
      saved.phase=saved.index===87?'entered':saved.id?'chat':'idle';saved.error=probeError(saved.error);
    }
    restoredAttempt=!!(saved.error||saved.pending||saved.index);
    if(!['izlauncher','izpslauncher'].includes(saved.launcher))saved.launcher='izlauncher';
  });
  function log(message) { const entry={at:new Date().toISOString(),type:'civswitch',message:redact(message).slice(-1600)};events.push(entry);if(events.length>250)events.shift();const owner=attempt;if(owner)logQueue=logQueue.then(()=>ledger.append(owner.id,entry.type,entry.message)).catch(()=>{}); }
  const persist = () => saveJson(file, saved);
  function target(body) {
    const host = body.host || saved.host || connections.snapshot().devices.lan.address || settings.consoleAddress;
    if (!localIPv4(host)) throw Error('Enter the Switch IPv4 address in your local network');
    return host;
  }
  async function run(args) {
    return new Promise((resolve,reject) => {
      let output='', done=false, timer;
      const handle=launch(python,['-u',...args],{cwd:folder,env:{...process.env,...configuration?.env,
        NODE_PATH:path.join(folder,'installer-creator/node_modules')},stdio:['ignore','pipe','pipe']});
      child=handle;
      const finish=(error)=>{if(done)return;done=true;clearTimeout(timer);if(child===handle)child=null;error?reject(error):resolve(output);};
      timer=setTimeout(()=>{handle.kill('SIGTERM');},10*60*1000);
      handle.on('error',finish);
      for(const stream of [handle.stdout,handle.stderr])stream.on('data',data=>{
        output=(output+data).slice(-1024*1024);log(data.toString());
      });
      handle.on('close',(code,signal)=>finish(code===0&&!interrupted?null:Error(interrupted?'CivSwitch operation stopped':signal?'CivSwitch process stopped':output.slice(-1200)||'CivSwitch process failed')));
    });
  }
  async function verifyLoader(host){
    saved.phase='checking';ready=false;await persist();
    const result=await run([path.join(folder,'probe.py'),host,String(PORT)]);
    if(!result.includes('CIVSWITCH_LOADER_VERIFIED'))throw Error('The CivSwitch loader did not confirm the probe');
    ready=true;verifiedHost=host;saved.phase='ready';delete saved.phaseBeforeCheck;
    log('Remote JS Loader verified on TCP 9026.');
  }
  async function installSave(host){
    saved.phase='installing';await persist();await require('./save-installation.cjs').begin(root,'civswitch');
    const result=await run([path.join(folder,'installer-creator/install.py'),host,String(PORT)]);
    if(!result.includes('INSTALL DONE'))throw Error('Installation was not confirmed');
    ready=false;verifiedHost=null;saved.phase='installed';await require('./save-installation.cjs').confirm(root,'civswitch');log('INSTALL DONE. Restart the game, then choose Load Game.');
  }
  async function waitForLoader(host){
    const deadline=now()+loaderWaitMs;
    // Wireless local play may release normal networking. Retry only the
    // harmless loader probe; never repeat HID or installation to restore LAN.
    for(let attempt=0;;attempt++){
      if(interrupted)throw Error('CivSwitch operation stopped');
      try{await verifyLoader(host);return;}
      catch(error){
        if(interrupted||now()>=deadline||attempt>=60||!/CIVSWITCH_LOADER_(NOT_RUNNING|TIMEOUT|NETWORK_ERROR)/.test(error.message))throw error;
        if(attempt===0)log('Waiting for normal networking after Wireless play. USB input is complete; chat lines will not be repeated.');
        await pause(Math.min(2000,Math.max(0,deadline-now())));
      }
    }
  }
  async function refresh(force = false) {
    await loaded;
    if(preview||task||(!force&&Date.now()-checkedAt<10000))return {ok:true};
    if(checking)return checking;
    checking=(async()=>{
      // Normal Wireless play intentionally drops the ordinary console network.
      // Keep routine polling to the input device; LAN diagnostics are explicit.
      if(force)await connections.refresh();
      networkHost=saved.host||connections.snapshot().devices.lan.address||settings.consoleAddress||'';
      const results=await Promise.all([arm.inspect(),force?(async()=>{
        routeError='';
        if(!localIPv4(networkHost))return false;
        try{await routeLookup(networkHost);}catch(error){routeError=error.message;return false;}
        return networkProbe(networkHost);
      })():Promise.resolve(networkReachable)]);
      [inputStatus,networkReachable]=results;checkedAt=Date.now();
      return {ok:true};
    })().finally(()=>{checking=null;});
    return checking;
  }
  async function action(body) {
    if(actionBusy)throw Error('Wait for the current CivSwitch operation to finish');
    actionBusy=true;
    try{return await performAction(body);}finally{actionBusy=false;}
  }
  async function performAction(body) {
    await loaded;
    if(preview)throw Error('Preview cannot send input or payloads');
    if(task)throw Error('Wait for the current CivSwitch operation to finish');
    const command=body.action;
    if(body.networkMode!==undefined){if(!['lan','wifi'].includes(body.networkMode))throw Error('Choose LAN or Wi-Fi');saved.networkMode=body.networkMode;}
    if(command==='discard') {
      saved={phase:'idle',index:0,host:saved.host||'',launcher:saved.launcher||'izlauncher'};
      restoredAttempt=false;ready=false;verifiedHost=null;jobId=null;events.length=0;
      await persist();return {ok:true};
    }
    if(command==='set-host') {
      const host=target(body),mode=body.networkMode||connections.snapshot().devices.lan.mode||'lan';
      await connections.update({id:'lan',mode,ip:host});saved.host=host;
      ready=false;verifiedHost=null;networkReachable=false;checkedAt=0;await persist();return {ok:true};
    }
    if(command==='select-launcher') {
      if(!['izlauncher','izpslauncher'].includes(body.launcher))throw Error('Choose IZlauncher or IZPSlauncher');
      saved.launcher=body.launcher;await persist();return {ok:true};
    }
    if(command==='check-recovery'){
      if(saved.phase!=='uncertain'||!saved.pending||!arm.receipt)throw Error('No verifiable interrupted line is available');
      const receipt=await arm.receipt(saved.pending);
      const batch=Number.isInteger(receipt.recordedSourceThrough)&&receipt.recordedSourceThrough===86;
      const complete=!batch&&receipt.status==='complete'&&receipt.sourceLine===saved.index;
      const partial=receipt.status==='failed'&&receipt.step===7&&receipt.line===saved.index&&/contact|timeout/i.test(receipt.error||'');
      saved.recovery={available:batch||complete||partial,mode:batch?'watch':complete?'complete':partial?'partial':'blocked',index:saved.index,jobId:saved.pending,bootId:receipt.bootId};
      await persist();return {ok:true,recovery:saved.recovery};
    }
    if(command==='reset') {
      if(!body.confirm)throw Error('Restart the game/lobby and confirm a fresh chat before starting again');
      const packet=await payload(folder);info=packet;
      saved={phase:'chat',index:0,launcher:saved.launcher||'izlauncher',host:target(body),id:crypto.randomUUID(),payloadSha256:packet.manifest.payloadSha256};
      restoredAttempt=false;ready=false;await persist();return {ok:true};
    }
    if(command==='confirm-line') {
      if(saved.phase!=='review'||body.index!==saved.index)throw Error('No matching completed line awaits confirmation');
      info??=await payload(folder);
      saved.index++;saved.phase=saved.index===info.lines.length?'entered':'chat';delete saved.pending;
      await persist();return {ok:true};
    }
    if(!['type-line','auto-chat','resume-chat','prepare-recovery','watch-input','check-loader','install'].includes(command))throw Error('Unknown CivSwitch action');
    let host=command==='check-loader'&&body.automatic===true?'':target(body);if(host)saved.host=host;
    const previousPhase=saved.phase;
    let work,automaticStage='input';
    if(command==='auto-chat'||command==='resume-chat') {
      const resuming=command==='resume-chat';
      if(resuming&&(saved.phase!=='recovery-ready'||body.emptyField!==true||body.sameLobby!==true||saved.recovery?.index!==saved.index))throw Error('Confirm the empty chat field in the same lobby before continuing');
      if(!resuming&&body.confirm!==true)throw Error('Open a fresh lobby chat and confirm the game is ready before automatic input');
      if(body.installAfterSuccess===true&&body.deleteSingleSaves!==true)throw Error('Automatic save installation deletes every single-player save. Back up needed saves and confirm deletion before starting.');
      if(!['cardputer-wifi','ssh-gadget','local-gadget'].includes(configuration.inputAdapter))throw Error('Choose ADV or ARM with USB keyboard + controller for automatic input');
      const device=await arm.inspect();
      if(!device.gamepadInput||!device.boundedLineInput||!device.keyboard)throw Error(configuration.inputAdapter==='cardputer-wifi'?'Install dual HID ADV firmware 0.22.3 and connect its USB data cable to Switch':'Prepare ARM USB keyboard + controller in Device setup before automatic input');
      if(!resuming&&saved.phase!=='idle'&&!(saved.phase==='chat'&&saved.index===0))throw Error('Start a fresh chat input sequence before automatic input');
      info??=await payload(folder);
      if(configuration.inputAdapter==='cardputer-wifi'&&!arm.stagePlans)throw Error('The input transport cannot stage automatic chat input');
      if(arm.stagePlans)await arm.stagePlans({civswitch:automaticPlan(folder)},event=>log(event.detail));
      const startIndex=resuming?saved.index:0;
      if(resuming&&saved.payloadSha256!==info.manifest.payloadSha256)throw Error('Payload changed; the previous lobby cannot be resumed');
      saved={phase:'auto-typing',index:startIndex,launcher:saved.launcher,host,id:crypto.randomUUID(),payloadSha256:info.manifest.payloadSha256,autoSaveRequested:body.installAfterSuccess===true};
      ready=false;
      work=async()=>{
        if(device.batchRawLines===true){
          jobId=crypto.randomUUID();saved.pending=jobId;await persist();
          // The entire source and controller sequence are local before execution.
          // Persist only monotonically increasing receipts for fully committed lines.
          let progressWrites=Promise.resolve();
          try{
            await arm.performPlan({...automaticPlan(folder,startIndex,jobId),sourceThrough:info.lines.length-1},event=>{
              log(event.detail||'Automatic input progress');
              const line=event.fields?.committedLine;
              if(Number.isInteger(line)&&line>saved.index&&line<=info.lines.length){
                saved.index=line;
                progressWrites=progressWrites.then(()=>persist());
              }
            });
          }finally{await progressWrites;}
          if(interrupted)throw Error('CivSwitch operation stopped');
          saved.index=info.lines.length;delete saved.pending;await persist();
        }else{
          while(saved.index<info.lines.length){
            if(interrupted)throw Error('CivSwitch operation stopped');
            jobId=crypto.randomUUID();saved.pending=jobId;await persist();
            await arm.performPlan(automaticPlan(folder,saved.index,jobId),event=>log(event.detail||'Automatic input progress'));
            if(interrupted)throw Error('CivSwitch operation stopped');
            saved.index++;delete saved.pending;await persist();
          }
        }
        jobId=null;saved.phase='entered';await persist();automaticStage='verify';
        log('All 87 lines sent. Waiting for the loader, then checking TCP 9026 automatically.');
        await pause(1500);
        await waitForLoader(host);
        if(body.installAfterSuccess===true){
          if(interrupted)throw Error('CivSwitch operation stopped');
          automaticStage='install';await installSave(host);
        }
      };
    } else if(command==='watch-input'){
      if(saved.phase!=='uncertain'||!saved.pending||!arm.followExisting||saved.recovery?.mode!=='watch')throw Error('Check the stored ADV job before reconnecting');
      jobId=saved.pending;saved.phase='auto-typing';
      work=async()=>{
        await arm.followExisting(jobId,event=>{
          log(event.detail||'Stored input progress');const line=event.fields?.committedLine;
          if(Number.isInteger(line)&&line>saved.index&&line<=87)saved.index=line;
        });
        saved.index=87;delete saved.pending;jobId=null;saved.phase='entered';await persist();
        automaticStage='verify';await waitForLoader(host);
        // An old persisted save choice is never reused as deletion consent.
      };
    } else if(command==='prepare-recovery'){
      if(saved.phase!=='uncertain'||body.sameLobby!==true||!saved.recovery?.available||saved.recovery.index!==saved.index||!arm.receipt)throw Error('Confirm that the interrupted lobby is still open before recovery');
      const previous=await arm.receipt(saved.recovery.jobId);
      if(previous.bootId!==saved.recovery.bootId||previous.id!==saved.recovery.jobId)throw Error('The interrupted input device changed');
      if(saved.recovery.mode==='complete'){
        if(previous.status!=='complete'||previous.sourceLine!==saved.index)throw Error('The completed line receipt changed');
        saved.index++;saved.recovery.index=saved.index;saved.phase=saved.index===87?'entered':'recovery-ready';delete saved.pending;await persist();return {ok:true};
      }
      if(previous.status!=='failed'||previous.step!==7||previous.line!==saved.index)throw Error('The interrupted line may have been submitted; automatic clearing is unavailable');
      jobId=crypto.randomUUID();saved.pending=jobId;saved.phase='recovery-clearing';
      work=async()=>{
        // Cancel the unsubmitted keyboard text and reopen an empty field.
        // No source characters and no Enter are sent in this operation.
        await arm.performPlan({id:jobId,steps:[{type:'key',key:'escape',delayMs:800},{type:'gamepad',button:'a',holdMs:50,delayMs:1200}]},event=>log(event.detail||'Clearing the interrupted chat field'));
        saved.phase='recovery-ready';delete saved.pending;log('Check the empty field on Switch before continuing. Confirmed earlier chat lines are preserved.');
      };
    } else if(command==='type-line') {
      if(saved.phase!=='chat'||body.index!==saved.index||body.chatReady!==true)throw Error('Open the chat input with controller A and confirm the current line');
      info??=await payload(folder);
      if(saved.payloadSha256!==info.manifest.payloadSha256)throw Error('Payload changed: restart the game/lobby and the input sequence');
      if(!['ssh-gadget','local-gadget','cardputer-wifi'].includes(configuration.inputAdapter))throw Error('Choose ARM PC or Cardputer ADV');
      jobId=crypto.randomUUID();saved.pending=jobId;saved.phase='typing';
      work=async()=>{
        await arm.performPlan(sourcePlan(folder,saved.index,jobId),event=>log(event.detail||'Input progress'));
        saved.phase='review';log('Line entered. Confirm it appeared correctly in the Switch chat before continuing.');
      };
    } else if(command==='check-loader') {
      saved.phaseBeforeCheck=previousPhase;saved.phase='checking';ready=false;
      work=async()=>{
        if(body.automatic===true){
          search=new AbortController();
          const found=await discoverNetwork({mode:body.networkMode||'auto',known:discovery.knownHosts(connections.snapshot(),saved.host),signal:search.signal});
          if(interrupted)throw Error('CivSwitch operation stopped');
          if(found.length!==1)throw Error(found.length?'Several game loaders found. Enter the IP shown on your Switch.':'No game loader found. Open Load Game, wait for Awaiting payload, and check LAN or Wi-Fi. You can enter the IP shown on Switch manually.');
          host=found[0];saved.host=host;log('Game loader found at '+host+':'+PORT);
        }
        await verifyLoader(host);
      };
    } else {
      if(!ready||verifiedHost!==host)throw Error('Check Remote JS Loader at this address before installing the save trigger');
        if(body.deleteSingleSaves!==true)throw Error('Installation deletes every file in save:/saves/Single/. Back up needed saves and explicitly confirm deletion.');
        work=()=>installSave(host);
    }
    interrupted=false;restoredAttempt=false;
    delete saved.error;
    // Persist the job identity before issuing any HID/network request. A lost
    // response or panel restart never silently repeats an accepted chat line.
    await persist();
    attempt={id:crypto.randomUUID(),upstream:true,loaderId:'civswitch',kind:'civswitch',status:'starting',created:new Date().toISOString()};await ledger.write(attempt);
    task=Promise.resolve().then(work).then(()=>{attempt.status='complete';}).catch(error=>{
      saved.phase=command==='check-loader'?(previousPhase==='ready'?'idle':previousPhase):['auto-chat','resume-chat','watch-input'].includes(command)&&automaticStage==='verify'?'entered':'uncertain';
      delete saved.phaseBeforeCheck;saved.error=redact(command==='check-loader'||automaticStage==='verify'?probeError(error.message):error.message);
      ready=false;verifiedHost=null;attempt.status=interrupted?'cancelled':'failed';attempt.error=saved.error;log(saved.error);
    }).finally(async()=>{try{await logQueue;await persist();attempt.completed=new Date().toISOString();await ledger.write(attempt);lastAttempt=attempt;}finally{jobId=null;attempt=null;task=null;}});
    return {ok:true,started:true};
  }
  async function stop({beforeSignal}={}) {
    await loaded;
    if(task){interrupted=true;search?.abort();if(child)child.kill('SIGTERM');if(jobId)await arm.halt(jobId);await task;}
    await beforeSignal?.();ready=false;return {ok:true};
  }
  async function snapshot() {
    await loaded;
    info??=await payload(folder);
    const inputReady=inputStatus?.moduleReady&&inputStatus?.keyboard;
    const chatPhase=['idle','chat','typing','auto-typing','review'].includes(saved.phase)||(saved.phase==='uncertain'&&saved.index<info.lines.length);
    const wirelessNetworkPending=!routeError&&!ready&&!networkReachable&&(chatPhase||saved.phase==='checking');
    return {preview,checks:{ready:!!inputReady,items:[
      {id:'server',ready:true,detail:'CivSwitch · PC connects to Switch on TCP '+PORT},
      {id:'arm',ready:!!inputStatus?.reachable,pending:!!inputStatus?.pending,detail:inputStatus?.pending?inputStatus.error:inputStatus?.reachable?(configuration.inputAdapter==='cardputer-wifi'?'ADV connected over Wi-Fi':'ARM connected; input service ready'):inputStatus?.error||'Use Device setup to prepare input'},
      {id:'usb',ready:!!inputStatus?.keyboard,pending:!!inputStatus?.pending,detail:inputStatus?.detail||inputStatus?.usbState||'USB keyboard not checked'},
      {id:'lan',ready:ready||networkReachable,pending:wirelessNetworkPending,port:PORT,detail:ready?'Remote JS Loader verified on TCP '+PORT:networkReachable?'Switch reachable at '+networkHost+'; Remote JS Loader on TCP '+PORT+' not verified':routeError|| (wirelessNetworkPending?'No LAN reply. Wireless lobby can disconnect normal networking. USB chat input can continue; the loader is checked after 87 lines.':networkHost?'No local network reply from '+networkHost:'Enter the Switch IPv4 address; loader readiness is checked separately')},
    ]},active:task?{id:attempt?.id||saved.id||'civswitch',kind:'civswitch',status:['typing','auto-typing'].includes(saved.phase)?'typing':'starting',progress:{line:saved.index,total:info.lines.length}}:null,
    lastAttempt,history:await ledger.listAttempts(),historyAll:true,log:events,library:[],jobs:[],installed:[],hostModules:{},connected:ready,
    settings:{consoleHost:settings.consoleHost,consoleAddress:networkHost||saved.host,consolePort:PORT},connections:{...connections.snapshot(),locked:!!task},
    sourcePreparation:{ready:true,preloaded:true,lines:0},setupWizard:false,hardStopAvailable:false,
    civswitch:{...saved,restoredAttempt,total:info.lines.length,busy:!!task,loaderReady:ready,inputStatus,networkReachable,networkHost,port:PORT,routeError,wirelessNetworkPending,
      automaticInputAvailable:inputStatus?.gamepadInput===true&&inputStatus?.boundedLineInput===true&&inputStatus?.keyboard===true,
      suggestedHost:saved.host||connections.snapshot().devices.lan.address||settings.consoleAddress||'',
      line:info.lines[saved.index]||'',saveName:'IZ2S.Civ7Save',saveInstallation:await require('./save-installation.cjs').read(root,saved),
      launcherStatus:{state:'unavailable',requires:['CivSwitch native-rpc','CivSwitch gpu-app/1'],
        detail:'CivSwitch launcher support is not implemented yet. Both launchers currently require the SmileBASIC runtime.'}}};
  }
  return {action,snapshot,refresh,stop,waitForOperation:()=>task||Promise.resolve(),hardStop:stop,ownsListener:()=>!!task,
    begin:()=>{throw Error('Use the CivSwitch controls in Core only');},
    setupAction:action,configureConnection:async body=>{if(task)throw Error('Wait for input to finish');await connections.update(body);if(body.id==='lan'){saved.host='';networkReachable=false;checkedAt=0;await persist();}ready=false;verifiedHost=null;return {ok:true};},
    retry:async()=>({ok:true}),setupReport:async()=>JSON.stringify(saved),historyLog:id=>ledger.all(id),dismiss(){}};
}
module.exports={create,localIPv4};
