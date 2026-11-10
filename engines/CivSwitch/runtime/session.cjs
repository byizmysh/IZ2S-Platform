'use strict';
const fs=require('node:fs/promises'),path=require('node:path'),crypto=require('node:crypto'),{spawn}=require('node:child_process');
const {readJson,saveJson,redact,createLedger}=require('../../../core/session/ledger.cjs');
function wrap(base,{root,preview=false,launch=spawn,configuration={},stopGraceMs=5000},kind){
  const field=kind==='civswitch-ldn'?'civswitchLdn':'civswitch';
  let processHandle=null,completion=null,posting=false,run=null,shell='izlauncher',lastError='',needsRestart=false;
  const events=[],ledger=createLedger(root);let logQueue=Promise.resolve(),automatic=null,automaticTask=null,launchEpoch=0;
  let stopTask=null,baseStopTask=null,shutdownUnconfirmed=false;
  const shutdownMessage='Civ7 shutdown could not be confirmed. Resources are retained. Restart Civilization VII before starting a new session.';
  function within(pending,deadline){return new Promise((resolve,reject)=>{const timer=setTimeout(()=>resolve({finished:false}),Math.max(0,deadline-Date.now()));Promise.resolve(pending).then(value=>{clearTimeout(timer);resolve({finished:true,value});},error=>{clearTimeout(timer);reject(error);});});}
  let libraryCache=null,libraryRead=null,libraryReadAt=0,libraryShell=null,libraryErrors=[];
  async function librarySnapshot(){
    const selected=shell==='original'?'izlauncher':shell;
    if(libraryCache&&libraryShell===selected&&Date.now()-libraryReadAt<10000)return libraryCache;
    if(libraryRead)return libraryRead;
    libraryRead=(async()=>{
      const repository=require('../../../core/packages/repository.cjs');
      const repo=await repository.create(root,{shell:selected,engine:kind,source:'current'});
      const listing=await repo.list(),packages=listing.packages.map(repository.publicPackage);
      libraryErrors=Array.isArray(listing.errors)?listing.errors:[];
      libraryCache=packages;libraryShell=selected;libraryReadAt=Date.now();return packages;
    })().finally(()=>{libraryRead=null;});
    return libraryRead;
  }
  const selectionFile=path.join(root,'data/state/civ-launcher.json'),selection=readJson(selectionFile,{}).then(saved=>{if(['izlauncher','izpslauncher','original'].includes(saved.shell))shell=saved.shell;});
  function log(message){const entry={at:new Date().toISOString(),type:'civ7-launcher',message:redact(message).slice(-2000)};events.push(entry);if(events.length>250)events.shift();if(run)logQueue=logQueue.then(()=>ledger.append(run.id,entry.type,entry.message)).catch(()=>{});}
  async function status(){
    if(!run)return {state:shutdownUnconfirmed?'stopping':'idle',shell,needsRestart,error:lastError,shutdownUnconfirmed,automaticLaunch:!!automatic};
    const report=await readJson(path.join(run.directory,'run11-runtime.json'),null);
    return {state:shutdownUnconfirmed?'stopping':run.status,shell,needsRestart,error:lastError,shutdownUnconfirmed,directory:run.directory,report,automaticLaunch:!!automatic};
  }
  async function start(body){
    if(posting||processHandle||stopTask||shutdownUnconfirmed)throw Error('Stop the current launcher first');
    posting=true;
    const epoch=++launchEpoch,check=()=>{if(epoch!==launchEpoch||body.cancelled)throw Error('Launcher startup cancelled');};
    try{
      if(preview)throw Error('Preview cannot start a console launcher');
      if(!['izlauncher','izpslauncher'].includes(body.launcher))throw Error('Choose IZlauncher or IZPSlauncher');
      const state=await base.snapshot(),current=state[field];check();
      if(current.busy||!current.loaderReady)throw Error('Connect to the game loader on TCP 9026 first');
      const host=current.suggestedHost||current.host;
      if(!require('../session.cjs').localIPv4(host))throw Error('Enter the Switch IPv4 address');
      if(needsRestart){
        const prior=await readJson(path.join(run.directory,'civswitch-connection.json'),null);
        const info=await require('./client.cjs').create({host}).connect();
        if(prior&&['main','sdk'].every(name=>Number(info.modules.find(m=>m.name===name)?.base)===prior.addresses[name]))throw Error('Restart Civilization VII and open Load Game before another graphics session');
        needsRestart=false;
      }
      let moduleIds=body.moduleIds||[];
      if((!Array.isArray(moduleIds)||moduleIds.length>15||moduleIds.some(id=>typeof id!=='string'||!/^[a-z0-9][a-z0-9-]{0,47}$/.test(id))))throw Error('Choose compatible applications');
      moduleIds=await require('../../../core/session/preload.cjs').resolve(root,moduleIds,{shell:body.launcher,engine:kind});check();
      shell=body.launcher;lastError='';await saveJson(selectionFile,{shell});
      const directory=await fs.mkdtemp(path.join(root,'data/sessions/launcher-'));check();
      run={id:crypto.randomUUID(),directory,status:'starting',started:new Date().toISOString()};
      await ledger.write({id:run.id,upstream:true,loaderId:kind,kind:'launch',status:'starting',created:run.started,runDirectory:directory});check();
      const child=launch(process.execPath,[path.join(__dirname,'run.cjs'),host,shell,directory,JSON.stringify(moduleIds)],{cwd:root,stdio:['ignore','pipe','pipe'],env:{...process.env,...configuration.env}});
      processHandle=child;
      completion=new Promise(resolve=>{
        let finished=false;
        const finish=(code,error)=>{if(finished)return;finished=true;processHandle=null;needsRestart=true;run.status=error||code!==0?'failed':'stopped';lastError=error?.message||(code!==0?'Civ7 launcher stopped unexpectedly. Restart the game and open Load Game.':'');if(lastError)log(lastError);logQueue.then(()=>ledger.write({id:run.id,upstream:true,loaderId:kind,kind:'launch',status:run.status==='failed'?'failed':'stopped',created:run.started,connectedAt:run.connectedAt,completed:new Date().toISOString(),runDirectory:run.directory,error:lastError})).catch(()=>{}).finally(resolve);};
        child.once('error',error=>finish(null,error));child.once('close',code=>finish(code));
        for(const stream of [child.stdout,child.stderr]){
          let buffer='';stream?.on('data',chunk=>{buffer+=chunk;const lines=buffer.split(/[\r\n]+/);buffer=lines.pop().slice(-4096);for(const line of lines){log(line);if(!stopTask&&!shutdownUnconfirmed&&line.startsWith('CIV7_LAUNCHER_READY ')){run.status='running';run.connectedAt||=new Date().toISOString();logQueue=logQueue.then(()=>ledger.write({id:run.id,upstream:true,loaderId:kind,kind:'launch',status:'running',created:run.started,connectedAt:run.connectedAt,runDirectory:run.directory})).catch(()=>{});}if(!stopTask&&!shutdownUnconfirmed&&line.startsWith('CIV7_LAUNCHER_OFFLINE ')){run.status='offline';lastError='PC connection to Civ7 was lost. Resources retained; stop this session and restart the game before launching again.';}}});
          stream?.once('end',()=>{if(buffer)log(buffer);});
        }
      });
      return {ok:true,started:true};
    }finally{posting=false;}
  }
  async function snapshot(){
    await selection;const state=await base.snapshot(),runtime=await status();
    const running=!stopTask&&!shutdownUnconfirmed&&run?.status==='running'&&!!processHandle;
    const library=await librarySnapshot();let jobs=[],installed=[];
    if(run){jobs=await require('../../../core/packages/jobs.cjs').all(run.directory);installed=processHandle?runtime.report?.installed||[]:[];}
    return {...state,changing:posting||!!automatic,library,moduleErrors:[...(state.moduleErrors||[]),...libraryErrors],log:[...state.log,...events],...(run?{jobs,installed,connected:running,
      active:processHandle?{id:run.id,kind:'launch',status:run.status,runDirectory:run.directory}:null}:{}),
      ...(run?{lastAttempt:{id:run.id,status:run.status,kind:'launch'}}:{}),history:await ledger.listAttempts(),historyAll:true,
      ...(shutdownUnconfirmed?{active:{...(processHandle?{id:run.id,kind:'launch',runDirectory:run.directory}:state.active||{kind:'launch'}),status:'stopping',nativeParentRetention:{enabled:true,awaitingActualClose:true,nativeCleanupProven:false}}}:{}),
      civLauncher:runtime,[field]:{...state[field],busy:state[field].busy||!!processHandle||!!automatic||!!stopTask||shutdownUnconfirmed,launcher:shell,launcherStatus:runtime}};
  }
  function stop(options){
    if(stopTask)return stopTask;
    stopTask=(async()=>{
      const deadline=Date.now()+stopGraceMs;
      launchEpoch++;const pendingAutomatic=automaticTask;
      if(automatic){automatic.cancelled=true;automatic=null;}
      const retain=error=>{
        shutdownUnconfirmed=true;needsRestart=true;lastError=shutdownMessage;
        if(processHandle)run.status='stopping';
        log(shutdownMessage+(error?' '+redact(error.message):''));
        return {ok:false,retained:true,restartRequired:true,error:shutdownMessage};
      };
      try{
        if(processHandle){run.status='stopping';await fs.writeFile(path.join(run.directory,'civswitch-loop.stop'),'stop\n',{mode:0o600});processHandle.kill('SIGTERM');}
        // Close shared host services only after the renderer actually exits.
        // A timeout keeps the owner fenced; neither a deadline nor SIGTERM
        // proves native cleanup. Setup-only cancellation can start at once.
        baseStopTask||=Promise.resolve(completion).then(()=>base.stop(options)).finally(()=>{baseStopTask=null;});
        const pending=Promise.all([completion,baseStopTask,pendingAutomatic]);
        const finished=()=>{shutdownUnconfirmed=false;if(lastError===shutdownMessage)lastError='';};
        pending.then(finished,()=>{});
        const result=await within(pending,deadline);
        if(!result.finished)return retain();
        finished();return result.value[1]||{ok:true};
      }catch(error){return retain(error);}
    })().finally(()=>{stopTask=null;});
    return stopTask;
  }

  return {...base,snapshot,stop,hardStop:stop,historyLog:id=>run?.id===id?ledger.all(id):base.historyLog(id),remove:id=>ledger.removeFailure(id,processHandle?run?.id:undefined),ownsListener:()=>!!stopTask||shutdownUnconfirmed||posting||!!automatic||!!processHandle||base.ownsListener(),
    async action(body){
      await selection;
      if(body.action==='select-shell'){if(processHandle||posting||automatic||stopTask||shutdownUnconfirmed)throw Error('Stop the current launcher first');if(!['izlauncher','izpslauncher','original'].includes(body.launcher))throw Error('Choose a console menu');shell=body.launcher;await saveJson(selectionFile,{shell});return {ok:true};}
      if(automatic)throw Error('Wait for automatic launcher startup or stop the session');
      if(body.action==='launch')return start(body);
      if(processHandle||posting||stopTask||shutdownUnconfirmed)throw Error('Stop the current launcher first');
      const auto=body.launchAfterSuccess===true&&['start','auto-chat','resume-chat','watch-input','check-loader'].includes(body.action)&&['izlauncher','izpslauncher'].includes(body.launcher);
      const request=auto?{...body,cancelled:false}:null;
      if(request)automatic=request;
      let result;
      try{result=await base.action(body);}catch(error){if(automatic===request)automatic=null;throw error;}
      if(request&&result?.ok){
        const scheduled=Promise.resolve(base.waitForOperation?.()).then(async()=>{
          if(request.cancelled||automatic!==request)return;
          const state=await base.snapshot(),current=state[field];
          // INSTALL DONE requires a fresh Load Game before graphics. A failed
          // or uncertain delivery must never be repeated by this continuation.
          if(request.cancelled||automatic!==request||current.error||current.busy||current.phase==='installed'||!current.loaderReady)return;
          log('Game loader ready. Starting '+request.launcher+' automatically.');
          await start(request);
        }).catch(error=>{if(!request.cancelled){lastError=redact(error.message);log('Automatic launcher startup failed: '+lastError);}})
          .finally(()=>{if(automatic===request)automatic=null;if(automaticTask===scheduled)automaticTask=null;});
        automaticTask=scheduled;
      }else if(automatic===request)automatic=null;
      return result;
    },
    async loadPackages(ids,options={}){
      if(!processHandle||run.status!=='running')throw Error('Start and connect the launcher first');
      const repo=await require('../../../core/packages/repository.cjs').create(root,{shell,engine:kind,source:'current'}),list=await repo.list();
      const plan=require('../../../core/packages/install-plan.cjs').create(list.packages,ids,{...options,storageReady:false});
      for(const {pkg,target}of plan)await require('../../../core/packages/jobs.cjs').create(run.directory,pkg,'browser',{target,expectedSha256:pkg.sha256});
      return {ok:true};
    },
    async unloadPackages(ids,target='session'){
      if(!processHandle||run.status!=='running')throw Error('Start and connect the launcher first');
      if(target!=='session')throw Error('Civ7 launcher currently uses session memory');
      const state=await snapshot();return require('../../../core/packages/removal.cjs').queue(run.directory,ids,state.installed,'browser',{target});
    }};
}
module.exports={wrap};
