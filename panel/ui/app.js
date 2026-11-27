'use strict';
// BEGIN ERROR TEXT
/* Canonical English diagnostics; localized UI labels never become log sources. */
'use strict';
(function(root){
 if(root.IZ2S_ERRORS){if(typeof module==='object'&&module.exports)module.exports=root.IZ2S_ERRORS;return;}
 let dictionary={},matcher;
 const cyrillic=/[\u0400-\u04ff]/;
 function configure(messages={}){
  const candidates=new Map();
  for(const [english,translated] of Object.entries(messages)){
   if(!cyrillic.test(english)&&cyrillic.test(translated)&&translated.length>=3&&!candidates.has(translated))candidates.set(translated,english);
  }
  dictionary=Object.fromEntries(candidates);const escape=s=>s.replace(/[.*+?^${}()|[\]\\]/g,'\\$&');
  const keys=[...candidates.keys()].sort((a,b)=>b.length-a.length);
  matcher=keys.length?new RegExp(keys.map(escape).join('|'),'g'):null;
 }
 function canonical(value){
  const source=String(value?.message??value??'');
  if(!cyrillic.test(source))return source;
  // Filenames/paths remain data, even when they contain non-English characters.
  const protectedParts=[];
  let text=source.replace(/(?:[A-Za-z]:[\\/]|\/)[^\s"'<>]+|`[^`]*`/g,part=>{protectedParts.push(part);return '\u0001'+(protectedParts.length-1)+'\u0002';});
  if(matcher)text=text.replace(matcher,p=>dictionary[p]);
  const legacy={'\u041e\u0448\u0438\u0431\u043a\u0430':'Error','\u043e\u0448\u0438\u0431\u043a\u0430':'error','\u041f\u0435\u0440\u0435\u0437\u0430\u043f\u0443\u0441\u043a':'Restart','\u041f\u043e\u0434\u043a\u043b\u044e\u0447\u0435\u043d\u0438\u0435':'Connection','\u043a\u043e\u043d\u0441\u043e\u043b\u044c':'console','\u041a\u043e\u043d\u0441\u043e\u043b\u044c':'Console','\u0424\u0430\u0439\u043b':'File','\u0444\u0430\u0439\u043b\u043e\u0432':'files','\u043e\u0441\u0442\u0430\u043d\u043e\u0432\u043b\u0435\u043d\u043e':'stopped','\u0432\u044b\u0431\u0440\u0430\u043d\u043e':'selected','\u0437\u0430\u0432\u0435\u0440\u0448\u0435\u043d\u043e':'completed','\u0412\u0432\u043e\u0434 \u043e\u0442\u043c\u0435\u043d\u0451\u043d. \u041f\u0435\u0440\u0435\u0437\u0430\u043f\u0443\u0441\u0442\u0438\u0442\u0435 \u043a\u043e\u043d\u0441\u043e\u043b\u044c \u043f\u0435\u0440\u0435\u0434 \u0441\u043b\u0435\u0434\u0443\u044e\u0449\u0435\u0439 \u043f\u043e\u043f\u044b\u0442\u043a\u043e\u0439.':'Input was cancelled. Restart the console before another attempt.'};
  for(const [ru,en]of Object.entries(legacy).sort((a,b)=>b[0].length-a[0].length))text=text.split(ru).join(en);
  // Unknown legacy prose is kept in the original downloadable log, never
  // spliced into an English/Spanish/Chinese interface or a native stack trace.
  text=text.split('\n').map(line=>cyrillic.test(line)?'Original diagnostic is available in the full log.':line).join('\n');
  return text.replace(/\u0001(\d+)\u0002/g,(_,i)=>protectedParts[Number(i)]);
 }
 function describe(value){
  const detail=canonical(value),s=(String(value?.message??value??'')+' '+detail).toLowerCase();let title,action;
  if(/fs rpc memory acquisition failed|fs acquisition stopped|overwrite missed.*canary/.test(s)){title='Filesystem access was not acquired in this attempt.';action='Fully close and reopen the game, open a fresh Direct, then start a new session.';}
  else if(/resources retained|uncertain|did not (?:finish|terminate)|restart required|native execution/.test(s)){title='Native execution could not be confirmed.';action='Restart the game before starting a new session.';}
  else if(/9026|loader.*not (?:respond|found)|awaiting payload/.test(s)){title='The game loader is not responding.';action='Open Load Game on Switch, wait for Awaiting payload, then check the connection.';}
  else if(/reopen.*(?:application|launch link)|this window link is no longer valid/.test(s)){title='This window link is no longer valid.';action='Open 0.34 from its shortcut.';}
  else if(s.includes('iz2s file manager is unavailable')){title='IZ2S file manager is unavailable.';action='Open IZ2S on Switch and connect its PC link, then try again.';}
  else if(s.includes('iz2s pc link is unavailable')){title='IZ2S PC link is unavailable.';action='Start the PC link, then try again.';}
  else if(s.includes('browser blocked the new tab')){title='The browser blocked the new tab.';action='Allow pop-ups for 0.34, then try again.';}
  else if(/package changed|selected package|version.*(?:differ|mismatch)/.test(s)){title='The selected package has changed.';action='Refresh App library and select the current version.';}
  else if(/failed to fetch|socket hang up|disconnected|connection.*(?:lost|refused|ended)|server.*unavailable/.test(s)){title='Connection was interrupted.';action='Check the network and restore the connection when the console is ready.';}
  else if(/timeout|timed out|aborted due/.test(s)){title='The operation timed out.';action='Check the device and connection before trying again.';}
  else if(/cancelled|canceled/.test(s)){title='The operation was cancelled.';action='Review the current console screen before continuing.';}
  else {title=detail.split('\n').find(l=>l.trim())||'The operation failed.';if(title.length>220)title='The operation failed.';action=detail&&detail.trim()!==title?'Open technical details to see the original error.':'';}
  return {title,action,detail};
 }
 const api={canonical,describe,configure};root.IZ2S_ERRORS=api;
 if(typeof module==='object'&&module.exports){module.exports=api;configure(require('./ru/ui.json'));}
})(globalThis);
// END ERROR TEXT
// BEGIN PRESENTATION
'use strict';
(function(root){
 function session(state){
  const civ=state?.civLauncher,active=state?.active;
  const running=active?.status==='running'||civ?.state==='running';
  const retained=!!active||['running','offline','starting','stopping'].includes(civ?.state);
  const offline=civ?.state==='offline'||active?.status==='offline'||!!state?.offline||retained&&!!state?.link&&['waiting-for-dock','waiting-for-network','offline'].includes(state.link.state);
  const stopping=active?.status==='stopping'||civ?.state==='stopping';
  const unconfirmed=stopping&&(active?.nativeParentRetention?.awaitingActualClose||civ?.shutdownUnconfirmed);
  return {running,offline,stopping,unconfirmed:!!unconfirmed,compact:running||offline||stopping&&active?.kind!=='bootstrap',title:unconfirmed?'Shutdown unconfirmed':stopping?'Disconnecting\u2026':offline?'Connection interrupted':running?'Session is running':'Connection',game:'Civilization VII',launcher:(civ?.shell||state?.platform?.variant)==='izpslauncher'?'IZPSlauncher':(civ?.shell||state?.platform?.variant)==='original'?'Core only':'IZ2S'};
 }
 function directNetwork(state){
  if(!state?.platform||['civswitch','civswitch-ldn','iz2sciv-core'].includes(state.platform.loaderId)||state.active?.kind==='bootstrap')return false;
  const initializing=state.active?.kind==='launch'&&state.active.launchStage==='initializing';
  return !!state.connected||initializing||session(state).compact||connectionMode(state).available;
 }
 function deviceLinks(cards){
  return cards.slice(1).map((next,i)=>{
   const card=cards[i],sameRow=Math.abs(card.y-next.y)<2;
   const a=sameRow?{x:card.x+card.width,y:card.y+card.height/2}:{x:card.x+card.width/2,y:card.y+card.height};
   const b=sameRow?{x:next.x,y:next.y+next.height/2}:{x:next.x+next.width/2,y:next.y};
   const x=(a.x+b.x)/2,y=(a.y+b.y)/2;
   let d;
   if(sameRow)d=`M ${a.x} ${a.y} C ${x} ${a.y} ${x} ${b.y} ${b.x} ${b.y}`;
   else if(Math.abs(a.x-b.x)<2)d=`M ${a.x} ${a.y} L ${b.x} ${b.y}`;
   else {const direction=Math.sign(b.x-a.x),r=Math.max(0,Math.min(6,(b.y-a.y)/4));d=`M ${a.x} ${a.y} V ${y-r} Q ${a.x} ${y} ${a.x+direction*r} ${y} H ${b.x-direction*r} Q ${b.x} ${y} ${b.x} ${y+r} V ${b.y}`;}
   return {d,x,y,tone:card.tone==='lost'||next.tone==='lost'?'lost':card.tone==='ready'&&next.tone==='ready'?'ready':'waiting'};
  });
 }
 function job(job){
  const complete=['complete','ready'].includes(job.status),removed=job.operation==='remove';
  const storedOnly=job.target==='sd'&&job.nativeCatalogAccepted===false;
  return {complete,removed,label:complete?(removed?(job.target==='sd'?'Removed from SD':'Removed from session'):job.target==='sd'?(storedOnly?'Stored on SD':'Installed on SD'):'Ready in menu'):job.status,failed:job.status==='failed',active:['queued','loading','publishing'].includes(job.status)};
 }
 const phases={verify:'Checking files',prepare:'Preparing on PC',graphics:'Preparing graphics',install:'Installing on PC',transfer:'Sending files to console','sd-prepare':'Preparing SD application','sd-write':'Writing to SD','sd-verify':'Verifying SD','sd-publish':'Waiting for the SD menu'};
 function transfer(state){
  const rows=(state?.jobs||[]).filter(j=>['queued','loading','publishing'].includes(j.status));
  const current=rows.filter(j=>j.status!=='queued').sort((a,b)=>String(b.progress?.updated||'').localeCompare(String(a.progress?.updated||'')))[0]||rows[0];if(!current)return null;
  const p=current.progress||{},known=Number.isFinite(p.totalBytes)&&p.totalBytes>0&&Number.isFinite(p.completedBytes),completed=known?Math.max(0,Math.min(p.totalBytes,p.completedBytes)):null;
  return {id:current.id,name:current.name||current.packageId,count:rows.length,label:phases[p.phase]||'Waiting for console',percent:known?Math.floor(100*completed/p.totalBytes):null,completed,total:known?p.totalBytes:null,target:current.target==='sd'?'SD':'RAM'};
 }
 function connectionMode(state){
  const active=state?.active,link=state?.link,retained=active?.kind==='launch'&&['running','offline'].includes(active?.status);
  const available=!!retained&&link?.protocol===3&&link?.recoveryAvailable===true;
  const online=available&&state.connected===true&&active.status==='running'&&link.state==='connected';
  const synchronizing=available&&['synchronizing','waiting-for-command-completion'].includes(link.state);
  return {available,online,synchronizing,retained:!!active,preferred:available?'restore':'new',canRestore:available&&!online&&!synchronizing,
   label:online?'Connection restored':synchronizing?'Synchronizing session…':'Restore connection',
   note:online?'Connected to the existing launcher. Its applications remain loaded.':synchronizing?'Authenticating the console and synchronizing pending commands.':available?'Keep the launcher open on Switch. Select Restore connection here or the Restore connection tile on the console.':active?.status==='stopping'?'Session is stopping. Wait for confirmed cleanup before reconnecting.':active?'This active session has no available recovery service. Stop it before starting a new session.':'Restore is available while this PC server retains a launcher session with automatic recovery enabled.'};
 }
 function sameSdPackage(sd,pkg){
  if(!sd?.versionVerified||!pkg)return false;
  const image=pkg.coreSd?.imageSha256;
  return image ? sd.sha256===image : !!pkg.sha256&&sd.sourceSha256===pkg.sha256;
 }
 function versions(state,pkg){
  const has=row=>(typeof row==='string'?row:row?.id)===pkg.id,session=!state.platform?.sdLauncher&&(state.installed||[]).find(has),sd=[...(state.sdInstalled||[]),...(state.platform?.sdLauncher?state.installed||[]:[])].find(has);
  return {pc:pkg.version,session:session?.version||null,sd:sd?.versionVerified?sd.version:null,onSd:!!sd,onSession:!!session,sdUnknown:!!sd&&!sd.versionVerified,sdUpdate:!!sd?.versionVerified&&!sameSdPackage(sd,pkg)};
 }
 function library(state){
  const packages=[...(state.library||[])],known=new Set(packages.map(p=>p.id));
  if(['civswitch','civswitch-ldn','iz2sciv-core'].includes(state.platform?.loaderId))return packages;
  for(const entry of [...(state.sdInstalled||[]),...(state.platform?.sdLauncher?state.installed||[]:[])]){
   const row=typeof entry==='string'?{id:entry}:entry;if(!row?.id||known.has(row.id))continue;known.add(row.id);
   packages.push({...row,name:row.name||row.id,version:row.versionVerified?row.version:null,bytes:row.bytes||0,icon:row.id==='iz-reconnect'?'/reconnect.svg':row.icon||'/icon.svg',description:'Installed on SD. The PC package is not available.',supported:false,sdOnly:true,sd:{compatible:true}});
  }
  return packages;
 }
 // Console inventory only: never infer RAM presence from PC files or startup choices.
 function consoleApps(state){
  if(!state?.connected)return [];
  const civ=['civswitch','civswitch-ldn','iz2sciv-core'].includes(state.platform?.loaderId),sdMode=!!state.platform?.sdLauncher;
  const ram=new Set((sdMode?[]:state.installed||[]).map(row=>typeof row==='string'?row:row.id));
  const sd=new Set((civ?[]:[...(state.sdInstalled||[]),...(sdMode?state.installed||[]:[])]).map(row=>typeof row==='string'?row:row.id));
  const packages=library(state),known=new Set(packages.map(p=>p.id));
  for(const entry of state.installed||[]){const row=typeof entry==='string'?{id:entry}:entry;if(row?.id&&!known.has(row.id)){packages.push({id:row.id,name:row.name||row.id,icon:row.id==='iz-reconnect'?'/reconnect.svg':row.icon||'/icon.svg'});known.add(row.id);}}
  return packages.filter(p=>ram.has(p.id)||sd.has(p.id)).map(p=>({id:p.id,name:p.name,icon:p.icon||'/icon.svg',ram:ram.has(p.id),sd:sd.has(p.id)}));
 }
 // Status is evaluated for the selected destination, so “Not installed + SD”
 // also finds apps which already have a separate RAM copy.
 function appFormat(app){
  if(app.format==='.nro'||app.format==='.iza')return app.format;
  return app.module?.switch||app.sd?.compatible||app.coreSd?.compatible||app.sdOnly?'.iza':null;
 }
 function libraryFilter(state,{query='',status='all',location='all',format='all',focusId=null}={}){
  state||={};const q=query.trim().toLocaleLowerCase(),rows=library(state).filter(p=>(!focusId||p.id===focusId)&&(format==='all'||appFormat(p)===format)&&(!q||[p.name,p.description,p.id].join(' ').toLocaleLowerCase().includes(q)));
  const present=p=>{const v=versions(state,p);return location==='ram'?v.onSession:location==='sd'?v.onSd:v.onSession||v.onSd;};
  const installed=rows.filter(present).length;
  return {ids:rows.filter(p=>status==='uninstalled'?!present(p):status==='installed'||location!=='all'?present(p):true).map(p=>p.id),total:rows.length,installed,uninstalled:rows.length-installed};
 }
 const terminal=new Set(['failed','cancelled','complete','stopped']);
 const errorLabels={filesystem:'Filesystem access',network:'Connection',timeout:'Timeout',input:'Input',graphics:'Graphics',application:'Application',native:'Native execution',cancelled:'Cancellation',other:'Other'};
 function errorType(row){
  if(['cancelled','stopped'].includes(row.status))return 'cancelled';
  const text=(String(row.error||'')+' '+(root.IZ2S_ERRORS?.canonical(row.error)||'')).toLowerCase();
  if(/fs[ -]?rpc|filesystem|file system|acquiring fs|fs memory|\u0444\u0430\u0439\u043b\u043e\u0432|\u0444\u0441 rpc/.test(text))return 'filesystem';
  if(/did not terminate|native execution|resources retained|uncertain.*native|native batch/.test(text))return 'native';
  if(/timeout|timed out|time.?out|\u0432\u0440\u0435\u043c\u044f \u043e\u0436\u0438\u0434\u0430\u043d\u0438\u044f/.test(text))return 'timeout';
  if(/fetch|socket|network|9026|connection|econn|host.*unreachable|game loader|awaiting payload|wi-fi|\u043f\u043e\u0434\u043a\u043b\u044e\u0447|\u0441\u043e\u0435\u0434\u0438\u043d\u0435\u043d/.test(text))return 'network';
  if(/hid|keyboard|input|chat|receipt|\u0432\u0432\u043e\u0434|\u043a\u043b\u0430\u0432\u0438\u0430\u0442/.test(text))return 'input';
  if(/vulkan|render|graphics|display|gpu|\u0433\u0440\u0430\u0444\u0438\u043a/.test(text))return 'graphics';
  if(/package|application|module|launcher stopped|\.iza|\u043f\u0440\u0438\u043b\u043e\u0436|\u043c\u043e\u0434\u0443\u043b/.test(text))return 'application';
  return 'other';
 }
 function attempt(row,{activeId}={}){
  const success=['complete','running'].includes(row.status)||!!row.connectedAt&&['running','offline','stopped'].includes(row.status);
  const outcome=success?'successful':terminal.has(row.status)?'unsuccessful':'pending';
  const label={failed:'Failed',cancelled:'Cancelled',complete:'Completed',stopped:success?'Completed':'Stopped',running:'Running',offline:'Connection interrupted',starting:'Starting',typing:'Entering commands',stopping:'Stopping'}[row.status]||'Interrupted';
  return {success,outcome,label,errorType:outcome==='unsuccessful'?errorType(row):null,tone:outcome==='unsuccessful'?'error':success?'success':'neutral',deletable:terminal.has(row.status)&&row.id!==activeId&&!row.active};
 }
 function inPeriod(row,period,now){
  if(period==='all')return true;const created=new Date(row.created),today=new Date(now);if(!Number.isFinite(created.getTime()))return false;
  today.setHours(0,0,0,0);const since=new Date(today);if(period==='week')since.setDate(since.getDate()-6);
  const until=new Date(today);until.setDate(until.getDate()+1);return created>=since&&created<until;
 }
 function statistics(rows,{period='today',now=Date.now()}={}){
  const result={total:0,successful:0,unsuccessful:0,pending:0,errors:{}};
  for(const row of rows){if(!inPeriod(row,period,now))continue;const info=attempt(row);result.total++;result[info.outcome]++;if(info.errorType)result.errors[info.errorType]=(result.errors[info.errorType]||0)+1;}
  return result;
 }
 function history(rows,{query='',kind='all',category='all',period='all',now=Date.now(),limit=10}={}){
  const q=query.trim().toLowerCase();const filtered=rows.filter(r=>{
   const info=attempt(r);return (kind==='all'||info.outcome===kind||r.status===kind)&&(category==='all'||info.errorType===category)&&inPeriod(r,period,now)&&(!q||[r.error,r.created,r.kind,r.status].join(' ').toLowerCase().includes(q));
  }).sort((a,b)=>String(b.created||'').localeCompare(String(a.created||'')));
  return {rows:filtered.slice(0,limit),total:filtered.length,more:filtered.length>limit};
 }
 const api={session,connectionMode,directNetwork,deviceLinks,job,transfer,versions,sameSdPackage,library,consoleApps,appFormat,libraryFilter,attempt,history,statistics,errorLabels};root.IZ2S_PRESENTATION=api;if(typeof module==='object'&&module.exports)module.exports=api;
})(globalThis);
// END PRESENTATION
// BEGIN STORAGE CONTROLS
'use strict';
// Shared destination policy and controls for the library and module tabs.
// Rendering reads the existing snapshot only; it never polls the console.
(function(root){
 const civ=state=>['civswitch','civswitch-ldn','iz2sciv-core'].includes(state?.platform?.loaderId);
 const targets=['session','sd'],label={session:'Session memory',sd:'SD card'},idOf=row=>typeof row==='string'?row:row?.id;
 function forStore(state){
  if(!state?.coreSd?.selected)return state;
  return {...state,connected:state.coreSd.ready,platform:{sdLauncher:true},
   sdRuntime:null,sdStorage:null,installed:[],jobs:[],sessionCatalog:[],
   sdInstalled:state.coreSd.apps||[],
   library:(state.library||[]).map(p=>({...p,sd:p.coreSd,supported:p.coreSd?.compatible===true}))};
 }
 function installed(state,id){return {session:!state?.platform?.sdLauncher&&(state?.installed||[]).some(row=>idOf(row)===id),sd:[...(state?.sdInstalled||[]),...(state?.platform?.sdLauncher?state?.installed||[]:[])].some(row=>idOf(row)===id)};}
 function compatible(state,pkg,target){
  if(target==='session')return !state?.platform?.sdLauncher&&pkg?.session?.compatible!==false;
  return target==='sd'&&!civ(state)&&pkg?.supported!==false;
 }
 function destination(state,pkg,preferences){
  if(pkg?.sdOnly)return 'sd';
  if(civ(state))return 'session';
  if(state?.platform?.sdLauncher)return 'sd';
  const chosen=preferences.get(pkg.id);if(targets.includes(chosen))return chosen;
  return pkg?.session?.compatible===false?'sd':'session';
 }
 function reason(state,pkg,target,{remove=false}={}){
  if(!targets.includes(target))return 'Choose an installation destination';
  if(state?.preview)return 'Preview · device commands disabled';
  if(civ(state)&&target==='sd')return 'Civilization VII currently loads applications into session memory.';
  if(!remove&&civ(state)&&pkg?.session?.compatible===false)return pkg.session.reason||'Unavailable for this game';
  if(state?.coreSd?.selected&&!state.coreSd.ready)return 'Reconnect the IZ2S SD card to this PC or refresh its inventory.';
  if(state?.coreSd?.selected&&!remove&&pkg?.coreSd?.compatible!==true)return pkg?.coreSd?.reason||'This module needs a standalone IZ2S package.';
  if(!state?.coreSd?.selected&&!civ(state)&&state?.platform?.variant==='original')return 'Choose a launcher in Connection to load applications.';
  if(!state?.connected)return 'Connect Switch 2 and start the launcher first';
  if(!remove&&pkg?.supported===false)return 'This module is unavailable for this launcher';
  if(target==='session'){
   if(state?.platform?.sdLauncher)return 'Session memory requires the PC launcher mode';
   if(!remove&&pkg?.session?.compatible===false)return pkg.session.reason||'This application requires the autonomous SD launcher';
   if(state?.platform?.residentGraphics){if(remove&&state.active)return 'Restart the resident session to change its prepared app set';const candidate=state.platform.residentFiniteAudio?pkg?.residentAudio:pkg?.resident;if(!remove&&candidate?.compatible!==true)return candidate?.userReason||'This application is unavailable in the retained memory mode';}
  }else{
   if(state?.platform?.sdLauncher){if(state.sdRuntime?.poisoned)return 'SD ownership is uncertain; restart the console before changing files';}
   else if(state?.platform?.residentGraphics||!state?.sdStorage?.enabled)return 'Start a fresh ordinary launcher session to enable SD installation';
   else if(state.sdStorage.state==='uncertain')return 'SD ownership is uncertain; restart the console before changing files';
  }
  if((state?.jobs||[]).some(j=>j.packageId===pkg?.id&&['queued','loading','publishing'].includes(j.status)))return 'Operation queued…';
  return '';
 }
 function filesystemWarning(state,pkg,target){
  return target==='session'&&state?.connected&&pkg?.session?.requiresFilesystem&&state?.fsRpc?.state!=='ready'
   ? 'Filesystem access is unavailable. This app can be stored in RAM, but cannot run in this session. Start a new session with Filesystem access enabled.' : '';
 }
 function payload(state,ids,preferences){
  if(!Array.isArray(ids)||!ids.length||ids.length>16)throw Error('Select available applications');
  const destinations={};for(const id of new Set(ids)){const pkg=state.library?.find(p=>p.id===id);if(!pkg)throw Error('Application is no longer available');const target=destination(state,pkg,preferences),error=reason(state,pkg,target);if(error)throw Error(error);destinations[id]=target;}
  if(Object.values(destinations).filter(target=>target==='sd').length>15)throw Error('Choose up to 15 SD applications');const uniform=[...new Set(Object.values(destinations))];return {ids:[...new Set(ids)],targets:destinations,...(uniform.length===1?{target:uniform[0]}:{})};
 }
 function create({document,getState,getBusy=()=>false,invoke,changed=()=>{},notice,confirm=()=>true,preferences:storage}){
  const preferences=new Map();try{for(const [id,value]of JSON.parse(storage?.getItem('iz2s.installTargets')||'[]'))if(/^[a-z0-9][a-z0-9-]{0,47}$/.test(id)&&targets.includes(value))preferences.set(id,value);}catch{}
  const el=(tag,className,text)=>{const e=document.createElement(tag);e.className=className||'';if(text!==undefined)e.textContent=text;return e;};
  const text=(node,value)=>{if(node.dataset.sourceText!==value){node.dataset.sourceText=value;node.textContent=value;}};
  const action=fn=>()=>Promise.resolve().then(fn).catch(error=>notice?.(error.message));
  function choose(id,target){preferences.set(id,target);try{storage?.setItem('iz2s.installTargets',JSON.stringify([...preferences].slice(-128)));}catch{}updateAll();changed();}
  function remove(pkg,target){return action(async()=>{
   const error=reason(getState(),pkg,target,{remove:true});if(error)throw Error(error);
   const question=target==='sd'?'Remove this application from SD? Games, BIOS files and saves will stay on the card.':'Remove this application from session memory and the next startup? Its SD copy and PC package will stay available.';
   if(!await confirm(question,pkg.name||pkg.id))return;
   await invoke('/api/unload',{ids:[pkg.id],target});
  });}
  function mount(pkg){
   const box=el('div','app-storage');box.dataset.appId=pkg.id;const field=el('label','storage-destination'),select=el('select','storage-target');field.append(el('span','','Install to'));
   select.setAttribute('aria-label','Installation destination');for(const target of targets){const option=el('option','',label[target]);option.value=target;select.append(option);}select.onchange=()=>choose(pkg.id,select.value);field.append(select);
   const hint=el('p','storage-hint'),badge=el('p','storage-installed'),identity=el('p','storage-versions'),actions=el('div','storage-actions'),install=el('button','secondary module-install');install.type='button';install.onclick=action(()=>invoke('/api/load',payload(getState(),[pkg.id],preferences)));actions.append(install);
   const manage=el('details','storage-more'),manageBody=el('div','storage-more-body menu-surface');manage.append(el('summary','','Manage installation'),manageBody);
   for(const target of targets){const button=el('button','secondary module-unload storage-remove-'+target,target==='sd'?'Remove from SD':'Remove from session');button.type='button';button.onclick=remove(pkg,target);manageBody.append(button);}
   badge.append(el('span','storage-badge-session','In RAM'),el('span','storage-badge-sd','On SD'));
   box.append(field,identity,hint,badge,actions,manage);update(box,pkg);return box;
  }
  function update(box,pkg){
   if(!box)return;const state=getState(),target=destination(state,pkg,preferences),select=box.querySelector('.storage-target'),error=reason(state,pkg,target),present=installed(state,pkg.id),busy=getBusy();
   select.value=target;select.disabled=busy||pkg.sdOnly===true;for(const option of select.children){option.disabled=!compatible(state,pkg,option.value);option.hidden=civ(state)&&option.value==='sd';}
   const warning=filesystemWarning(state,pkg,target);
   const note=pkg.sdOnly?'Installed on SD. The PC package is not available.':error||(warning?'RAM storage only. Enable FS in a new session to run.':null)||(target==='sd'?(pkg?.sd?.compatible&&!pkg.sd.hostRequired?'Runs from SD without the PC.':'Requires the PC server when launched.'):'Cleared when the game closes.');text(box.querySelector('.storage-hint'),note);box.querySelector('.storage-hint').title=warning||note;box.querySelector('.storage-hint').dataset.tone=warning&&!error?'warning':'';
   const badge=box.querySelector('.storage-installed');badge.hidden=!present.session&&!present.sd;box.querySelector('.storage-badge-session').hidden=!present.session;box.querySelector('.storage-badge-sd').hidden=!present.sd;
   const install=box.querySelector('.module-install');install.disabled=busy||!!error;install.title=error||warning;const versions=(root.IZ2S_PRESENTATION?.versions||((state,pkg)=>{const sd=state.sdInstalled?.find(r=>r.id===pkg.id);return {pc:pkg.version,onSession:!!state.installed?.find(r=>r.id===pkg.id),onSd:!!sd,sd:sd?.versionVerified?sd.version:null,session:state.installed?.find(r=>r.id===pkg.id)?.version};}))(state,pkg);
   const identity=box.querySelector('.storage-versions');const menu=(state.sessionCatalog||[]).find(r=>r.id===pkg.id);identity.hidden=!versions.onSession&&!versions.onSd; text(identity,(versions.onSession?'Session: '+(versions.session||'version unknown'):'')+(versions.onSd?(versions.onSession?' · ':'')+'SD: '+(versions.sd||'version unknown'):'')+(menu?.version&&menu.version!==pkg.version?' · Console catalog: '+menu.version:''));
   identity.title=identity.textContent;
   if(pkg.sdOnly){const blocked=reason(state,pkg,'sd',{remove:true});install.disabled=busy||!!blocked;install.title=blocked;install.onclick=remove(pkg,'sd');text(install,'Remove from SD');}
   else text(install,target==='sd'?(present.sd?(versions.sd&&versions.sd!==versions.pc?'Update on SD':'Reinstall on SD'):'Install on SD'):'Load into session');
   box.querySelector('.storage-more').hidden=pkg.sdOnly||!present.session&&!present.sd;
   for(const place of targets){const button=box.querySelector('.storage-remove-'+place);button.hidden=!present[place];const blocked=reason(state,pkg,place,{remove:true});button.disabled=busy||!!blocked;button.title=blocked;}
  }
  function updateAll(){for(const box of document.querySelectorAll('.app-storage')){const pkg=(root.IZ2S_PRESENTATION?.library(getState())||getState()?.library)?.find(p=>p.id===box.dataset.appId);if(pkg)update(box,pkg);}}
  function selection(ids){return payload(getState(),ids,preferences);}
  function selectionError(ids){try{selection(ids);return '';}catch(e){return e.message;}}
  return {mount,update,updateAll,selection,selectionError,preferences};
 }
 const api={installed,compatible,destination,reason,filesystemWarning,payload,create,forStore};root.IZ2S_STORAGE=api;if(typeof module==='object'&&module.exports)module.exports=api;
})(globalThis);
// END STORAGE CONTROLS
// BEGIN WORKSPACE TABS
globalThis.IZ2S_WORKSPACE = {create({document,api,notice,onSelect,invoke,storageControls,reopenLauncherFiles,retainRecord=()=>false}) {
  const tabs=document.getElementById('workspace-tabs'),home=document.getElementById('workspace-home'),panes=document.getElementById('workspace-tools'),dock=document.getElementById('workspace-dock');
  const rows=new Map(),records=new Map(),seen=new Map(),modules=new Map(),moduleViews=new Set(),closed=new Map();
  const launcherFilesId = 'launcher-files';
  const isLauncherFiles = id => id === 'ftp-files' || id === 'core-files';
  const workspaceRecord = record => isLauncherFiles(record.id)
    ? {...record, backendId:record.id, id:launcherFilesId} : record;
  let active='home',snapshot,available=true;
  const el=(tag,text,className)=>{const node=document.createElement(tag);if(text)node.textContent=text;if(className)node.className=className;return node;};
  const action=(label,fn,className)=>{const button=el('button',label,className);button.type='button';button.onclick=()=>Promise.resolve().then(fn).catch(error=>notice(error.message));return button;};
  const icon=name=>{const svg=document.createElementNS('http://www.w3.org/2000/svg','svg'),use=document.createElementNS(svg.namespaceURI,'use');svg.setAttribute('aria-hidden','true');use.setAttribute('href','#'+name);svg.append(use);return svg;};
  const label=(node,text)=>{if(node.dataset.sourceText!==text){node.dataset.sourceText=text;node.textContent=text;}};
  function statusState(record) {
    if (record.state !== 'running' || !record.backendId) return record.state;
    if (!available) return 'unavailable';
    const connection = record.connection || 'checking';
    return connection === 'connected' ? 'running' : connection;
  }
  function statusText(record) {
    if (record.pending) return 'Stopping…';
    if (record.opening) return 'Opening…';
    const labels = {
      running: 'Running', retained: 'Restart console required', stopped: 'Stopped',
      checking: 'Checking connection…', disconnected: 'Not connected',
    };
    return labels[statusState(record)] || 'Unavailable';
  }
  const moduleTitle=id=>{const module=modules.get(id);return module?.name+([...modules.values()].filter(item=>item.name===module?.name).length>1?' · '+(module.version||id):'');};
  try{for(const [id,value] of JSON.parse(sessionStorage.getItem('iz2s.closedTools')||'[]'))closed.set(id,value);}catch{}
  const remember=()=>{try{sessionStorage.setItem('iz2s.closedTools',JSON.stringify([...closed]));}catch{}};
  const mainWrap=el('div',null,'workspace-tab'),mainTab=action('Overview',()=>select('home','push'),'workspace-tab-label');
  mainWrap.dataset.tabId='home';mainTab.setAttribute('role','tab');mainTab.id='workspace-tab-home';mainTab.setAttribute('aria-controls','workspace-home');mainWrap.append(mainTab);tabs.append(mainWrap);
  function selection() {
    mainWrap.classList.toggle('active',active==='home');mainTab.setAttribute('aria-selected',String(active==='home'));mainTab.tabIndex=active==='home'?0:-1;
    for(const [id,row]of rows){const selected=id===active;row.pane.hidden=!selected;row.tab.classList.toggle('active',selected);row.button.setAttribute('aria-selected',String(selected));row.button.tabIndex=selected?0:-1;}
    for(const row of dock.children){const record=records.get(row.dataset.workspaceId),selected=row.dataset.workspaceId===active||(active.startsWith('module-info-')&&record?.kind==='module'&&record.moduleId===active.slice(12));row.classList.toggle('active',selected);const button=row.querySelector('button');if(selected)button.setAttribute('aria-current','page');else button.removeAttribute('aria-current');}
    tabs.hidden=rows.size===0;
    if(tabs.hidden){home.removeAttribute('role');home.removeAttribute('aria-labelledby');}
    else {home.setAttribute('role','tabpanel');home.setAttribute('aria-labelledby',mainTab.id);}
    document.body.classList.toggle('workspace-active',active!=='home');
  }
  function select(id, navigation = 'none') {
    if (isLauncherFiles(id)) id = launcherFilesId;
    if(id!=='home'&&!rows.has(id))return false;
    const changed = active !== id;
    active=id;home.hidden=id!=='home';panes.hidden=id==='home';selection();
    onSelect?.(id==='home'?{id:'home',title:'Overview'}:{id,title:rows.get(id).title}, changed ? navigation : 'none');
    return true;
  }
  function close(id) {
    const row=rows.get(id);if(!row)return;
    const hadFocus=row.pane.contains?.(document.activeElement)||row.tab.contains?.(document.activeElement);
    const record=records.get(id);if(record)closed.set(id,record.requestId);
    row.onClose?.();
    remember();row.pane.remove();row.tab.remove();rows.delete(id);if(active===id)select('home','replace');else selection();renderDock();
    if(hadFocus)(tabs.hidden?document.querySelector('#panel-sidebar [aria-current="page"]'):active==='home'?mainTab:rows.get(active)?.button)?.focus();
  }
  function make(id,title) {
    if(rows.has(id)){const row=rows.get(id);row.title=title;label(row.button,title);row.button.title=title;row.dismiss.setAttribute('aria-label','Close view: '+title);return row;}
    const tab=el('div',null,'workspace-tab'),button=action(title,()=>select(id,'push'),'workspace-tab-label'),dismiss=action('×',()=>close(id),'workspace-close'),pane=el('section',null,'workspace-pane');
    tab.dataset.tabId=id;button.setAttribute('role','tab');button.id='workspace-tab-'+id;button.setAttribute('aria-controls','workspace-pane-'+id);button.title=title;
    dismiss.setAttribute('aria-label','Close view: '+title);dismiss.title='Close view; the module keeps running';
    pane.id='workspace-pane-'+id;pane.setAttribute('role','tabpanel');pane.setAttribute('aria-labelledby',button.id);pane.hidden=true;tab.append(button,dismiss);tabs.append(tab);panes.append(pane);
    const row={tab,button,dismiss,pane,title};rows.set(id,row);selection();return row;
  }
  function shareLanguage(frame) {
    const language=globalThis.RUN21_LANGUAGE?.id;
    if(language)frame.contentWindow?.postMessage({type:'iz2s:workspace-language',language},new URL(frame.src).origin);
  }
  function renderRecord(record) {
    const row=make(record.id,record.title),ready=record.state==='running',retained=record.state==='retained';
    if(!row.content){
      row.toolbar=el('div',null,'workspace-tool-bar');row.status=el('span',null,'workspace-status');row.status.setAttribute('role','status');
      row.reload=action('Reload view',async()=>{const current=records.get(record.id);if(current?.state==='running'){if(row.frame)row.frame.src=row.frame.src;}else await startRecord(record.id);},'secondary');
      row.stop=action('Stop module',()=>stopRecord(record.id),'workspace-stop');
      const controls=el('div',null,'workspace-tool-actions');controls.append(action('Close view',()=>close(record.id),'secondary'),row.reload,row.stop);row.toolbar.append(row.status,controls);
      row.content=el('div',null,'workspace-content');row.pane.replaceChildren(row.toolbar,row.content);
    }
    label(row.status,statusText(record));row.status.dataset.state=statusState(record);
    label(row.reload,ready?'Reload view':'Start / open');row.reload.disabled=!!record.pending||!!record.opening||retained||(!available&&!ready)||(!ready&&!!snapshot?.preview);
    label(row.stop,record.pending?'Stopping…':record.kind==='settings'?'Stop settings':'Stop module');row.stop.disabled=!!record.pending||!!record.opening||retained||record.state==='stopped'||(record.coreTool||!record.moduleId&&record.kind!=='files')||!available||!!snapshot?.preview;
    const signature=JSON.stringify([record.url,record.state,!ready&&!available,!ready&&!!snapshot?.preview]);
    if(row.signature===signature)return row;
    row.signature=signature;row.content.replaceChildren();row.frame=null;
    if(!ready){
      const empty=el('div',null,'empty-state');
      empty.append(el('h2',retained?'Restart console required':record.state==='stopped'?'Tool stopped':'Tool unavailable'),
        el('p',retained?'Restart the console before starting this module again.':!available?'Reconnect the panel to start this tool.':
          snapshot?.preview?'Preview · device commands disabled':'Select Start / open to use the tool again.'));
      row.content.append(empty);
    }
    else{
      const frame=el('iframe'),target=new URL(record.url);if(record.kind==='files')target.searchParams.set('embedded','1');
      frame.name='iz2s-workspace';frame.title=record.title;frame.src=target.href;frame.referrerPolicy='no-referrer';
      frame.setAttribute('allow','fullscreen; display-capture; gamepad; clipboard-write');
      frame.setAttribute('sandbox','allow-scripts allow-same-origin allow-forms allow-downloads allow-modals allow-popups allow-pointer-lock');
      frame.onload=()=>shareLanguage(frame);row.frame=frame;row.content.append(frame);
    }
    return row;
  }
  async function startRecord(id) {
    const record=records.get(id);if(!record||record.pending||record.opening||!available)return;
    if (record.backendId) return reopenLauncherFiles(record.backendId);
    const previousClose=closed.get(id);
    record.opening=true;renderDock();if(rows.has(id))renderRecord(record);
    try{const result=await api('/api/workspace/open',{id});if(result.tab&&records.get(id)?.requestId===record.requestId){const dismissed=closed.get(id)!==previousClose;if(dismissed){closed.set(id,result.tab.requestId);remember();}openRequest(result.tab,!dismissed);}}
    finally{const current=records.get(id);if(current){current.opening=false;if(rows.has(id))renderRecord(current);}renderDock();}
  }
  async function stopRecord(id) {
    const record=records.get(id);if(!record||record.pending||record.opening||!available)return;
    record.pending=true;renderDock();if(rows.has(id))renderRecord(record);
    try{const result=await api('/api/workspace/stop',{id:record.backendId||id});if(!records.has(id)||records.get(id).requestId!==record.requestId)return;const current=result.tab?workspaceRecord(result.tab):records.get(id);if(!result.tab)current.state='stopped';records.set(id,{...current,title:record.title,pending:true});if(rows.has(id))renderRecord(records.get(id));}
    finally{const current=records.get(id);if(current){current.pending=false;if(rows.has(id))renderRecord(current);}renderDock();}
  }
  function validateRecord(record) {
    if(!record||!/^[-a-z0-9]{1,80}$/.test(record.id))throw Error('Invalid module tab');
    const url=new URL(record.url);if(url.protocol!=='http:'||url.hostname!=='127.0.0.1'||url.username||url.password)throw Error('Module interface must be local');
  }
  function openRequest(record,force=false) {
    validateRecord(record);
    record=workspaceRecord(record);
    const previous=records.get(record.id);
    const sameBackend=previous?.backendId===record.backendId;
    record={...record,...(record.kind==='files'?{title:'Super File Bros.'}:{}),pending:sameBackend&&previous?.pending,opening:sameBackend&&previous?.opening};
    records.set(record.id,record);const fresh=seen.get(record.id)!==record.requestId;seen.set(record.id,record.requestId);
    if(force||fresh&&!record.opening&&closed.get(record.id)!==record.requestId&&record.state!=='stopped'){closed.delete(record.id);remember();renderRecord(record);if(force||record.kind!=='files'&&!record.automatic)select(record.id,force?'push':'none');}
    else if(rows.has(record.id))renderRecord(record);
    renderDock();
  }
  function dockRow(id,title,status,callback,kind,state='stopped') {
    const row=el('div',null,'workspace-dock-row'),open=action('',callback,'workspace-dock-open'),text=el('span',null,'workspace-dock-text');
    row.dataset.workspaceId=id;row.dataset.state=state;open.title=title;text.append(el('span',title,'workspace-dock-name'));open.append(icon(kind==='files'?'i-folder':'i-grid'),text);row.append(open,el('small',status,'workspace-dock-state'));return {row,open};
  }
  function renderDock() {
    const localRows=[...rows].filter(([,row])=>row.local);
    const stamp=JSON.stringify([available,!!snapshot?.preview,localRows.map(([id,row])=>[id,row.title]),[...records.values()].map(r=>[r.id,r.title,r.url,r.requestId,r.state,r.connection,r.pending,r.opening,r.moduleId]),[...moduleViews].map(id=>[id,moduleTitle(id),snapshot?.installed,snapshot?.moduleStates])]);
    if(dock.dataset.stamp===stamp){selection();return;}dock.dataset.stamp=stamp;dock.replaceChildren();
    for(const record of records.values()){
      const {row,open}=dockRow(record.id,record.title,statusText(record),()=>!available||snapshot?.preview||['running','retained'].includes(record.state)?openRequest(record,true):startRecord(record.id),record.kind,record.pending?'pending':record.opening?'opening':statusState(record)),stop=action('',()=>stopRecord(record.id),'workspace-dock-stop');
      stop.append(icon('i-stop'));
      stop.setAttribute('aria-label','Stop '+record.title);stop.title='Stop '+record.title;stop.disabled=!!record.pending||!!record.opening||['stopped','retained'].includes(record.state)||(record.coreTool||!record.moduleId&&record.kind!=='files')||!available||!!snapshot?.preview;open.disabled=!!record.pending||!!record.opening;row.append(stop);dock.append(row);
    }
    for(const id of moduleViews){
      const module=modules.get(id);if(!module||[...records.values()].some(record=>record.moduleId===id&&record.kind==='module'))continue;
      const loaded=snapshot?.installed?.some(item=>(typeof item==='string'?item:item.id)===id),running=snapshot?.moduleStates?.some(item=>item.id===id&&item.state==='running');
      dock.append(dockRow('module-info-'+id,moduleTitle(id),running?'Running':loaded?'Loaded on console':'Not running',()=>openModule(id),'module',running?'running':loaded?'loaded':'stopped').row);
    }
    for(const [id,row] of localRows) {
      dock.append(dockRow(id,row.title,'PC streaming',()=>select(id,'push'),'settings','ready').row);
    }
    dock.hidden=!dock.childElementCount;document.getElementById('workspace-dock-title').hidden=dock.hidden;document.getElementById('workspace-dock-section')?.toggleAttribute('hidden',dock.hidden);const count=document.getElementById('workspace-dock-count');if(count)label(count,String(dock.childElementCount));selection();
  }
  function moduleContent(id) {
    const module=modules.get(id),row=rows.get('module-info-'+id);if(!module||!row)return;
    row.pane.replaceChildren();row.pane.classList.add('workspace-module-details');
    const loaded=snapshot?.installed?.some(item=>(typeof item==='string'?item:item.id)===id),running=snapshot?.moduleStates?.some(item=>item.id===id&&item.state==='running'),toolbar=el('div',null,'workspace-tool-bar'),controls=el('div',null,'workspace-tool-actions'),status=el('span',row.modulePending?'Operation in progress…':running?'Running':loaded?'Loaded on console':'Not running','workspace-status');
    status.dataset.state=running?'running':'stopped';status.setAttribute('role','status');row.pane.setAttribute('aria-busy',String(!!row.modulePending));toolbar.append(status,controls);
    const taskButton=(title,fn,disabled=false)=>{const button=action(title,async()=>{if(row.modulePending)return;row.modulePending=true;moduleContent(id);try{await fn();}finally{row.modulePending=false;if(rows.has('module-info-'+id))moduleContent(id);}},'secondary');button.disabled=disabled||!!row.modulePending;controls.append(button);};
    if(module.switch&&storageControls)controls.append(storageControls.mount(snapshot?.library?.find(pkg=>pkg.id===id)||{...module,id}));
    if(module.server&&!module.switch)taskButton(running?'Stop module':'Start module',()=>invoke(running?'/api/module/stop':'/api/module/start',{id}),!available||!!snapshot?.preview);
    if(module.server?.settings)taskButton(module.server.settings.label||'Settings',async()=>{const result=await api('/api/module/settings',{id});openRequest(result.tab,true);},!available||!!snapshot?.preview);
    const pkg=snapshot?.library?.find(item=>item.id===id),content=el('div',null,'workspace-module-copy'),heading=el('h1',module.name),intro=el('header',null,'page-title page-intro'),copy=el('div');
    heading.setAttribute('translate','no');copy.append(el('span','Application','eyebrow'),heading,el('p',module.version||pkg?.version||'','hint'));intro.append(copy);
    content.append(intro,el('p',pkg?.description||module.description||'','subtitle'));
    if(pkg?.session?.compatible===false)content.append(el('p',pkg.session.reason||'Unavailable for this game','hint'));
    else if(pkg?.session?.requiresFilesystem)content.append(el('p','Requires filesystem access','hint'));
    content.append(action('App library',()=>document.querySelector('#panel-sidebar [data-view="apps"]')?.click(),'secondary'));
    if(module.switch)content.append(el('p','This module runs on the console. Use this tab to load or remove it.','hint'));
    row.pane.append(toolbar,content);
  }
  function openLocal(id, title, createContent, onClose) {
    const row=make(id,title);
    if(!row.local) {
      row.local=true;
      row.onClose=onClose;
      row.pane.append(createContent());
      renderDock();
    }
    select(id,'push');
  }
  function openModule(id) {
    if(!modules.has(id))throw Error('Module is no longer installed');
    const record=[...records.values()].find(record=>record.moduleId===id&&record.kind==='module');if(record){openRequest(record,true);return rows.get(record.id);}
    const row=make('module-info-'+id,moduleTitle(id));moduleViews.add(id);moduleContent(id);select('module-info-'+id,'push');renderDock();return row;
  }
  function updateModules(state) {
    const stamp=JSON.stringify([state.moduleCatalog,state.moduleStates,state.installed,state.sdInstalled,state.sdStorage,state.jobs?.map(j=>[j.packageId,j.target,j.status]),state.library?.map(p=>[p.id,p.name,p.version,p.description,p.module,p.session,p.sd]),state.connected,state.preview,state.platform?.residentGraphics,state.platform?.sdLauncher]);
    const ended=snapshot?.connected&&(!state.connected||snapshot.active?.id!==state.active?.id);
    snapshot=state;if(updateModules.stamp===stamp&&!ended)return;updateModules.stamp=stamp;
    if(ended)for(const id of [...moduleViews]){moduleViews.delete(id);close('module-info-'+id);}
    modules.clear();for(const module of state.moduleCatalog||[])modules.set(module.id,module);
    // Imported IZA packages also have a details page, even without a source module.
    for(const pkg of state.library||[]){const id=pkg.module?.id||pkg.id;if(!modules.has(id))modules.set(id,{...pkg.module,id,name:pkg.name,version:pkg.version,switch:pkg.module?.switch||{}});}
    for(const id of moduleViews){if(!modules.has(id)){close('module-info-'+id);moduleViews.delete(id);}else if(rows.has('module-info-'+id)){make('module-info-'+id,moduleTitle(id));moduleContent(id);}}
    for(const [id,record]of records)if(rows.has(id))renderRecord(record);renderDock();
  }
  function setAvailable(value){if(available===value)return;available=value;for(const [id,record]of records)if(rows.has(id))renderRecord(record);for(const id of moduleViews)if(rows.has('module-info-'+id))moduleContent(id);renderDock();}
  function sync(entries){
    const valid=entries.filter(entry=>{try{validateRecord(entry);return true;}catch{return false;}});
    const invalidCount = entries.length - valid.length;
    // Direct FTP and PC Link are two backends for the same launcher file view.
    // Prefer a backend with live console access over a disconnected window.
    const launcherFiles = valid.filter(entry => isLauncherFiles(entry.id));
    const runningFiles = launcherFiles.filter(entry => entry.state === 'running');
    const usableFiles = runningFiles.filter(entry => !['disconnected','unverified'].includes(entry.connection));
    const latest = latestRegistration(usableFiles) || latestRegistration(runningFiles) || latestRegistration(launcherFiles);
    const tools = valid.filter(entry => !isLauncherFiles(entry.id));
    if (latest) tools.push(workspaceRecord(latest));
    // Compatibility with a running panel predating held core-session tabs.
    for(const record of records.values())if(!tools.some(entry=>entry.id===record.id)&&retainRecord(record))tools.push(record);
    const present=new Set(tools.map(entry=>entry.id));
    for(const id of [...records.keys()])if(!present.has(id)){
      records.delete(id);seen.delete(id);closed.delete(id);close(id);
    }
    remember();for(const entry of tools)openRequest(entry);renderDock();
    return invalidCount;
  }
  function latestRegistration(entries) {
    return entries.reduce((latest,entry) => !latest || entry.updatedAt >= latest.updatedAt ? entry : latest, undefined);
  }
  tabs.addEventListener('keydown',event=>{
    if(!['ArrowLeft','ArrowRight','Home','End'].includes(event.key))return;
    const buttons=[...tabs.querySelectorAll('[role=tab]')],index=buttons.indexOf(document.activeElement);if(index<0)return;event.preventDefault();
    const next=event.key==='Home'?0:event.key==='End'?buttons.length-1:(index+(event.key==='ArrowRight'?1:-1)+buttons.length)%buttons.length;buttons[next].click();buttons[next].focus();
  });
  window.addEventListener('run21:languagechange',()=>{for(const row of rows.values())if(row.frame)shareLanguage(row.frame);});
  window.addEventListener('message',event=>{if(event.data?.type!=='iz2s:workspace-ready')return;for(const row of rows.values())if(row.frame&&event.source===row.frame.contentWindow&&event.origin===new URL(row.frame.src).origin)shareLanguage(row.frame);});
  select('home');return{select,openRequest,openModule,openLocal,updateModules,sync,close,setAvailable};
}};
// END WORKSPACE TABS
// BEGIN GENERATED RUN21_CATALOG
const RUN21_CATALOG = (function createPolicy() {
  const hidden = new Set([
    'cpu-mailbox-lab',
    'cpu-mailbox-lab-v2',
    'cpu-mailbox-lab-v3',
    'ims-driver-cache-probe',
    'ims-gpu-pcm-cityfast',
    'ims-music-packed-lab',
    'izbrowser-check-20261101',
    'izbrowser-offline-v2-20261101',
    'izbrowser-offline-v3-20261101',
    'izbrowser-offline-v4-20261101',
    'izbrowser-wifi-20261101',
    'izdoom-audio-test',
    'izpsx-sd-test',
    'izdoom-native18',
    'izdoom-wide18',
    'izoff-012',
    'izsonic-transfer18',
    'ns1-core-constructor-check',
    'ns1-memory-budget-check',
    'ns1-memory-budget-extended-check',
    'ns1-native-cached-memory-check',
    'ns1-paged-core-check',
    'ns1-paged-core-current-check',
    'ns1-paged-kernel-check',
    'ns1-paged-kernel-current-check',
    'ns1-runtime-check',
    'ns1-runtime-tls-check',
    'ns1-vulkan-capabilities-check',
    'ns1-vulkan-memory-check',
    'ns1-vulkan-pool-check',
    'ps2-cpu-interpreter-check',
    'ps2-cpu-lightweight-check',
  ]);
  const hiddenEngines = new Set(['pepsipu', 'izmysh-resume', 'civswitch', 'civswitch-ldn']);
  const izEngines = new Set(['iz2s-core']);
  const izVariants = new Set(['izlauncher']);

  function visible(descriptor) {
    return !hidden.has(descriptor.id) && !/^ps2-(?:cold-r|cpu-accounting-r|sd-admit-r|sd-engine-r)/.test(descriptor.id) && !['test','diagnostic'].includes(descriptor.store?.channel);
  }

  function engineAvailable(loader, variant) {
    if (!loader || hiddenEngines.has(loader.id) || loader.supported === false) return false;
    const declared = Array.isArray(loader.availableIn) ? loader.availableIn
      : Array.isArray(loader.modes) ? loader.modes : null;
    if (variant === 'original') return !declared || declared.includes('original');
    if (!izVariants.has(variant) || !izEngines.has(loader.id)) return false;
    // Older APIs declared only the common IZ2S transport mode.
    return !declared || declared.some(mode => izVariants.has(mode));
  }

  function normalizeSnapshot(snapshot) {
    if (!snapshot || typeof snapshot !== 'object' || Array.isArray(snapshot)) return snapshot;
    const result = {...snapshot};
    if (Array.isArray(snapshot.library)) result.library = snapshot.library.filter(visible);
    if (Array.isArray(snapshot.moduleCatalog)) result.moduleCatalog = snapshot.moduleCatalog.filter(visible);
    if (snapshot.platform && Array.isArray(snapshot.platform.loaders)) {
      result.platform = {
        ...snapshot.platform,
        loaders: snapshot.platform.loaders.filter(loader => !hiddenEngines.has(loader.id)).map(loader => ({
          ...loader,
          availableIn: ['original', 'izlauncher'].filter(variant => engineAvailable(loader, variant)),
        })),
      };
    }
    // Active sessions, installed apps, jobs, loaderId and connection state stay intact.
    return result;
  }

  return {visible, engineAvailable, normalizeSnapshot};
})();
// END GENERATED RUN21_CATALOG
if (new URLSearchParams(location.search).get('ui-preview') === 'files') {
  document.documentElement.lang = 'en';
  document.title = 'Filesystem · IZ2S preview';
  document.getElementById('panel-startup').hidden = true;
  document.getElementById('manual-files-document').hidden = false;
} else {
const $ = (id) => document.getElementById(id),
  key = location.hash.slice(1),
  selection = new Set();
const viewTitles = Object.freeze({setup:'Install and launch', apps:'App library', history:'Attempts', faq:'FAQ'});
let homeView = 'setup', workspaceView = {id:'home'};
function viewFromUrl() {
  const view = new URLSearchParams(location.search).get('view');
  return Object.hasOwn(viewTitles, view) ? view : 'setup';
}
function updateNavigation() {
  const title = workspaceView.id === 'home' ? viewTitles[homeView] : workspaceView.title;
  for (const button of document.querySelectorAll('#panel-sidebar [data-view]')) {
    const current = workspaceView.id === 'home' && button.dataset.view === homeView;
    button.classList.toggle('active', current);
    if (current) button.setAttribute('aria-current', 'page');
    else button.removeAttribute('aria-current');
  }
  document.title = (globalThis.RUN21_LANGUAGE?.t(title)||title) + ' · IZ2S Platform';
}
function storeNavigation(method) {
  const url = new URL(location.href);
  if (homeView === 'setup') url.searchParams.delete('view');
  else url.searchParams.set('view', homeView);
  history[method + 'State'](workspaceView.id === 'home' ? null : {run25Tool: workspaceView.id}, '', url);
}
function restoreNavigation(focus = true) {
  const tool = history.state?.run25Tool, view = viewFromUrl();
  switchView(view, {record:false, focus:false, focusApp:view==='apps'&&view===homeView});
  if (tool && workspace.select(tool)) {
    if (focus) document.getElementById('workspace-tab-' + workspaceView.id)?.focus({preventScroll:true});
    return;
  }
  if (tool) storeNavigation('replace');
  if (focus) $('page-' + homeView + '-title').focus({preventScroll:true});
}
let hardStopping = false;
let stopTask = null, pendingStart = false, actionGeneration = 0;
let cardputerRequest=null;
let state = null,
  serverUnavailable = false,
  step = 'launch',
  busy = false,
  notificationKey = '',
  dismissed = '',
  packageStamp = '',
  requestId = null,
  finishedAttempt = '',
  setupStamp = '',
  resultStamp = '',
  pendingSetup = null,
  logFollow = true,
  logWriting = false,
  backendNotice = '';
let startupPreferences;try{startupPreferences=localStorage;}catch{}
let startupStamp = '';
// The manual-input design is a local UI preview, separate from session settings.
let inputDesign = 'arm', manualDesignStage = 1;

let ftpConnection = null, ftpPoll = null, connectionAction = false;
const iz2scivUI = globalThis.IZ2S_IZ2SCIV.create({document,act,getLink:()=>{const value=coreView.current();return value?.refreshError?{online:false,error:value.refreshError.message}:value?.pcLink;},openLibrary:()=>openCorePcTool('apps'),openFiles:()=>openCorePcTool('files'),openFtp:openFtpDialog,getFtp:()=>ftpConnection,refreshLink:()=>Promise.all([coreView.refresh(true),refreshFtpStatus()]),setPcLink,closeFtp:disconnectFtp,resetSession:resetLauncherSession,isUnavailable:()=>serverUnavailable,isPanelBusy:()=>busy,showLog:()=>{iz2scivLogFull=true;renderActivity();$('session-activity').open=true;$('log').setAttribute('tabindex','-1');$('log').focus();$('session-activity').scrollIntoView({block:'nearest'});},downloadWifiReport:report=>saveDownload(new Blob([JSON.stringify(report,null,2)+'\n'],{type:'application/json'}),'iz2s-windows-wifi.json'),downloadSd:async()=>saveDownload(await api('/api/iz2sciv/sd-package',undefined,{responseType:'blob',timeoutMs:60000}),'iz2sciv-core-sd.zip')});

let workspaceReady = false;
let workspaceRevision = 0;
function openWorkspaceTab(tab, select = true) {
  // A workspace response requested before registration may omit the new tab.
  workspaceRevision++;
  workspace.openRequest(tab, select);
  if (select) $('workspace-tab-' + workspaceView.id)?.focus({preventScroll:true});
}
let installPreferences;try{installPreferences=localStorage;}catch{}
const storageState=()=>{
 const base=globalThis.IZ2S_STORAGE.forStore(state),core=coreView.current();
 if(!base||base.coreSd?.selected||!core?.session)return base;
 return {...base,coreLive:true,connected:!!core.pcLink?.online,platform:{sdLauncher:true},
  installed:[],sdInstalled:core.pcLink?.apps||[],sdRuntime:null,sdStorage:null};
};
const storageControls=globalThis.IZ2S_STORAGE.create({document,getState:storageState,getBusy:()=>busy||serverUnavailable,invoke:act,changed:()=>renderControls(),notice,confirm:(message,name)=>confirmRemoval(message,name),preferences:installPreferences});
const workspace = globalThis.IZ2S_WORKSPACE.create({document,api,notice,invoke:act,storageControls,
 reopenLauncherFiles:id=>id==='ftp-files'?openFtpDialog():openCorePcTool('files'),retainRecord:record=>{
 const core=coreView.current();
 return core?.fsRpc===undefined&&globalThis.IZ2S_CORE.rpcAvailable(core)&&record.kind==='files'&&record.state==='running'&&record.sessionId===core.attempt.id;
},onSelect:(tab,navigation)=>{
  workspaceView=tab;
  updateNavigation();
  if (navigation !== 'none') storeNavigation(navigation);
  if (navigation === 'push') window.scrollTo({top:0, left:0, behavior:'instant'});
}});
const manualDesignConfirmed = new Set();

;
const words = {
  server: ['Server x64 PC', 'i-server'],
  arm: ['ARM PC', 'i-arm'],
  usb: ['ARM Type C - Switch', 'i-usb'],
  lan: ['Console', 'i-lan'],
};
const element = (tag, className, text) => {
  const e = document.createElement(tag);
  if (className) e.className = className;
  if (text !== undefined) e.textContent = text;
  return e;
};
function setText(node, value) {
  const text = node.closest?.('[translate="no"],.app-name,pre,code')?String(value??''):globalThis.RUN21_LANGUAGE?.t(value)||String(value??'');
  if (node.textContent !== text) node.textContent = text;
}
function placeChild(parent, child, index) {
  if (parent.children[index] !== child)
    parent.insertBefore(child, parent.children[index] || null);
}
function folderButton(kind, id, text = 'Open folder') {
  const button = element('button', 'quiet folder-button');
  button.type = 'button';
  button.innerHTML = '<svg aria-hidden="true"><use href="#i-folder"/></svg>';
  button.append(element('span', '', text));
  button.setAttribute('aria-label', text);
  button.title = text;
  button.disabled = serverUnavailable || !!state?.preview;
  button.onclick = async event => {
    event.preventDefault();
    event.stopPropagation();
    const meta = state?.platform, target = id || ({
      configuration:meta?.configurationId, engine:meta?.loaderId, 'engine-source':meta?.loaderId,
      shell:meta?.variant || 'izlauncher',
    })[kind];
    button.dataset.pending = 'true';
    button.disabled = true;
    try { await api('/api/folder/open', { kind, ...(target ? { id:target } : {}) }); }
    catch (error) { notice(error.message); }
    finally { delete button.dataset.pending; renderLocalActionAvailability(); }
  };
  return button;
}
const date = (value) =>
  value ? new Date(value).toLocaleString(globalThis.RUN21_LANGUAGE?.locale || 'en-GB', { hour12: false }) : '';
async function api(route, data, options = {}) {
  // Only status reads have a deadline; actions may still be running on the server.
  const controller = data === undefined && options.timeoutMs > 0 ? new AbortController() : null;
  const deadline = controller ? setTimeout(() => controller.abort(), options.timeoutMs) : null;
  try {
    const res = await fetch(route, {
      method: data === undefined ? 'GET' : 'POST',
      headers: {
        'x-iz-key': key,
        ...(data === undefined ? {} : { 'content-type': options.upload ? 'application/octet-stream' : 'application/json' }),
      },
      ...(data === undefined ? {} : { body: options.upload ? data : JSON.stringify(data) }),
      ...(controller ? { signal: controller.signal } : {}),
    });
    if (res.ok && options.responseType === 'blob') return await res.blob();
    if (res.ok && options.responseType === 'response') return res;
    const result = await res.json().catch(() => null);
    if (!res.ok) throw Object.assign(Error(result?.error || 'The request failed (HTTP ' + res.status + ').'), {statusCode: res.status});
    if (result === null) throw Error('The server returned an invalid response.');
    return result;
  } finally {
    if (deadline !== null) clearTimeout(deadline);
  }
}
function saveDownload(blob, filename) {
  const url = URL.createObjectURL(blob), link = element('a');
  link.href = url;
  link.download = filename;
  link.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}
function notice(message, type = 'error', identity = '') {
  if (identity && identity === dismissed) return;
  const toast = $('toast'), success = type === 'success', warning = type === 'warning';
  if (identity && !toast.hidden && (notificationKey === identity || !notificationKey)) return;
  notificationKey = identity;
  toast.dataset.tone = success ? 'success' : warning ? 'warning' : 'error';
  $('toast-summary').setAttribute('role', success || warning ? 'status' : 'alert');
  const problem = globalThis.IZ2S_ERRORS.describe(message);
  setText($('toast-title'), success ? message : problem.title);
  $('toast-action').hidden = success || !problem.action;
  setText($('toast-action'), problem.action);
  const details = $('toast-details');
  details.open = false;
  details.hidden = success || !problem.detail || problem.detail.trim() === problem.title;
  setText($('toast-diagnostic'), problem.detail);
  toast.querySelector('.toast-symbol').textContent = success ? '✓' : '!';
  toast.hidden = false;
}
function setFeedback(id, message, tone = 'info') {
  const box = $(id);
  box.dataset.tone = tone;
  setText(box, message);
}
function renderIssueList(id, issues) {
  const box = $(id);
  box.hidden = !issues?.length;
  const raw = $(id + '-raw');
  setText(raw, (issues || []).map(issue =>
    (issue.id || issue.path || 'Item') + ': ' + globalThis.IZ2S_ERRORS.canonical(issue.error)).join('\n'));
  if (box.hidden) box.querySelector('details').open = false;
}
async function act(route, data = {}, {onError = error => notice(error.message)} = {}) {
  if (route === '/api/stop') return stopSession();
  if(state?.coreSd?.selected&&['/api/load','/api/unload','/api/sd/refresh'].includes(route))data={...data,store:'core-sd',selection:state.coreSd.selection};
  if (inputDesign === 'own' && !['/api/stop', '/api/hard-stop', '/api/dismiss'].includes(route)) {
    notice('This is a manual input preview. Changes are not sent to the device.');
    return;
  }
  if (busy || serverUnavailable) return;
  if (!notificationKey) $('toast').hidden = true;
  const generation = ++actionGeneration;
  busy = true;
  pendingStart = route === '/api/start';
  renderControls();
  try {
    const result = await api(route, data);
    if (generation === actionGeneration) await poll(true);
    return generation === actionGeneration ? result : undefined;
  } catch (e) {
    if (generation === actionGeneration) onError(e);
  } finally {
    if (generation === actionGeneration) {
      busy = false;
      pendingStart = false;
      renderControls();
    }
  }
}
function stopSession() {
  if (stopTask) return stopTask;
  // STOP bypasses queued server work. Keep it reachable during a pending
  // launch, and ignore that request's eventual UI result after cancellation.
  actionGeneration++;
  busy = true;
  pendingStart = false;
  document.querySelectorAll('.up-ready,.up-source-ready').forEach(input => {input.checked = false;});
  stopTask = Promise.resolve().then(async () => {
    try {
      const result = await api('/api/stop', {});
      if(result?.retained)notice(result.error||'Shutdown unconfirmed','error');
      // Completion must not wait on catalog, workspace or an older status read.
      void poll(true);
      return result;
    } catch (error) {notice(error.message);}
    finally {stopTask = null; busy = false; renderControls();}
  });
  renderControls();
  return stopTask;
}
async function resetLauncherSession() {
  const dialog = $('launcher-reset-dialog');
  dialog.returnValue = 'cancel';
  const choice = new Promise(resolve => {
    dialog.addEventListener('close', () => resolve(dialog.returnValue), {once:true});
  });
  dialog.showModal();
  if (await choice !== 'reset') return;
  const result = await act('/api/iz2s-core/session/reset');
  if (result?.ok) {
    void coreView.refresh(true);
    notice('Session reset. You can start First Setup.', 'success');
  }
  return result;
}
async function openCorePcTool(view) {
  if(view==='apps'){
    switchView('apps');
    if(state)renderLibrary();
    $('library-core-add').focus({preventScroll:true});
    return;
  }
  if (serverUnavailable || state?.preview || openingPcFiles) return;
  openingPcFiles = true;
  try {
    const target=await api('/api/iz2s-core/pc-tool?'+new URLSearchParams({view,open:'1'}),undefined,{timeoutMs:5000});
    if(!target.available||!target.tab)throw Error(view==='files'?'IZ2S file manager is unavailable.':'IZ2S PC link is unavailable.');
    openWorkspaceTab(target.tab);
  }catch(error){notice(error.message);}
  finally{openingPcFiles=false;}
}
let openingPcFiles = false;
const ftpDialog=$('ftp-dialog');
function refreshFtpStatus() {
  if (ftpPoll || !state || state.preview || serverUnavailable || state.platform?.loaderId!=='iz2sciv-core') return ftpPoll;
  ftpPoll = api('/api/iz2s-core/ftp/status', undefined, {timeoutMs:12000})
    .then(value => { ftpConnection = value; })
    .catch(() => { ftpConnection = {...ftpConnection,online:false,refreshError:true}; })
    .finally(() => { ftpPoll = null; iz2scivUI.render(state); renderConnectionStatus(); });
  return ftpPoll;
}
async function changeLauncherConnection(action) {
  if (connectionAction || serverUnavailable || state?.preview) return;
  connectionAction = true;
  try { await action(); }
  catch (error) { notice(error.message); }
  finally { connectionAction = false; }
}
function setPcLink(enabled) {
  return changeLauncherConnection(async () => {
    await api('/api/iz2s-core/pc/connection', {enabled});
    await coreView.refresh(true);
  });
}
function disconnectFtp() {
  return changeLauncherConnection(async () => {
    await api('/api/iz2s-core/ftp/close', {});
    await ftpPoll;
    await refreshFtpStatus();
  });
}
function openFtpDialog({reuse=false}={}){
  if(serverUnavailable||state?.preview||ftpDialog.open)return;
  if(reuse&&ftpConnection?.online&&ftpConnection.tab){
    openWorkspaceTab(ftpConnection.tab);
    return;
  }
  $('ftp-error').hidden=true;
  $('ftp-host').value='';
  $('ftp-host').placeholder=coreView.current()?.pcLink?.consoleAddress||'';
  ftpDialog.showModal();
}
document.addEventListener('click',event=>{
  if(event.target.closest('[data-open-ftp]'))openFtpDialog();
});
$('ftp-cancel').onclick=()=>ftpDialog.close();
ftpDialog.addEventListener('cancel',event=>{if($('ftp-connect').disabled)event.preventDefault();});
$('ftp-form').onsubmit=async event=>{
  event.preventDefault();
  const button=$('ftp-connect'),cancel=$('ftp-cancel'),error=$('ftp-error');
  if(button.disabled)return;
  button.disabled=cancel.disabled=true;
  error.hidden=true;
  try{
    const result=await api('/api/iz2s-core/ftp/open',{host:$('ftp-host').value.trim()});
    ftpDialog.close();
    openWorkspaceTab(result.tab);
    await ftpPoll;
    void refreshFtpStatus();
  }catch(failure){
    setText(error,failure.message);
    error.hidden=false;
    $('ftp-host').focus();
  }finally{button.disabled=cancel.disabled=false;}
};
const coreText = globalThis.IZ2S_CORE.t;
const coreView=globalThis.IZ2S_CORE.create({api,changed:()=>{renderPlatform();renderControls();;},updated:()=>{if(state){void refreshFtpStatus();iz2scivUI.render(state);;renderConnectionStatus();renderSessionPresentation();renderConsoleApps();;if(homeView==='apps')renderLibrary();updateBlitBunTab();showBlitBunWindowRequest();}},
 openFiles:()=>openCorePcTool('files'),
 openApps:()=>openCorePcTool('apps')});
function switchView(id, {focusApp=false, record=true, focus=true} = {}) {
  if (!Object.hasOwn(viewTitles, id)) return;
  const changed = homeView !== id || workspaceView.id !== 'home';
  if (changed) closeTransientMenus();
  if (!focusApp && libraryContext && leaveLibraryContext()) applyLibrarySearch();
  homeView = id;
  if (inputDesign === 'own' && id !== 'setup') {
    inputDesign = 'arm';
    if (state) { ; renderControls(); }
  }
  for (const view of document.querySelectorAll('#workspace-home > .view')) view.hidden = view.id !== id;
  workspace.select('home');
  if (record && changed) storeNavigation('push');
  if (changed) {
    window.scrollTo({top:0, left:0, behavior:'instant'});
    if (focus && !focusApp) $('page-' + id + '-title').focus({preventScroll:true});
  }
}

let sessionModeChoice=null,sessionModeScope='';

function renderControls(){
 if(!state)return;
 $('library-core-add').hidden=!!state.preview||!(state.coreSd?.selected||coreView.current()?.pcLink?.online);
 $('library-core-add').disabled=busy||serverUnavailable;
 $('library-core-add').textContent=coreText('addApp');
 const error=selection.size?storageControls.selectionError([...selection]):'';
 $('load').disabled=busy||!(state.coreSd?.selected?state.coreSd.ready:coreView.current()?.pcLink?.online)||!selection.size||!!state.preview||!!error;
 $('load').title=error;storageControls.updateAll();
 document.querySelectorAll('.module-host-actions button').forEach(button=>{button.disabled=busy||!!state.preview||button.dataset.unavailable==='true';});
 iz2scivUI.render(state);renderServerAvailability();
}
function renderWizard(){renderControls();}

const networkName = (mode) => (mode === 'wifi' ? 'Wi-Fi' : 'LAN');

document.addEventListener('click', event => {
  closeTransientMenus(event.target);
  if (!event.target.closest('.detail-help'))
    document.querySelectorAll('.detail-help[aria-expanded="true"]').forEach(button => button.setAttribute('aria-expanded', 'false'));
});
const transientMenus = 'details:is(.device-picker,.address-editor,#connection-files,.app-more,.storage-more)[open]';
function closeTransientMenus(except) {
  for (const menu of document.querySelectorAll(transientMenus))
    if (!except || !menu.contains(except)) menu.open = false;
}
document.addEventListener('keydown', event => {
  if (event.key !== 'Escape' || document.querySelector('dialog[open]')) return;
  const menus = [...document.querySelectorAll(transientMenus)].filter(menu => menu.getClientRects().length);
  const help = document.querySelector('.detail-help[aria-expanded="true"]:not([hidden])');
  if (!menus.length && !help) return;
  const menu = menus.find(item => item.contains(event.target)) || menus.at(-1);
  closeTransientMenus();
  if (help) help.setAttribute('aria-expanded', 'false');
  (menu?.querySelector(':scope > summary') || help)?.focus({preventScroll:true});
  event.preventDefault();
});

document.querySelectorAll('[data-manual-stage]').forEach(button => {
  button.onclick = () => { manualDesignStage = Number(button.dataset.manualStage); ; };
});
;
;
;
;

// Change the topology only at the handoff from input hardware to networking.
// Repeated status polls keep the same DOM and never replay this transition.

function selectedShellName() {
  const id=['civswitch','civswitch-ldn','iz2sciv-core'].includes(state?.platform?.loaderId)?state.civLauncher?.shell||'izlauncher':state?.platform?.variant;
  return id==='original'?'Core only':state?.platform?.shells?.find(shell=>shell.id===id)?.name||(id==='izpslauncher'?'IZPSlauncher':'IZ2S');
}

;
;
let libraryContext=null;
let shownBlitBunWindowRequest;
let blitBunTab;

function mediaPhaseLabel(source) {
  return {
    choosing:'Choose a tab in the browser sharing dialog.',
    starting:'Starting video…',
    retrying:'Video interrupted. Retrying automatically.',
    failed:'Video stopped. Start it again on this PC.',
    running:source?.audio==='muted'?'Video playing without audio.':'Video source running.',
  }[source?.phase] || '';
}

function updateBlitBunTab() {
  const tab=blitBunTab;
  if (!tab?.panel.isConnected) return;
  const browser=tab.capture?.status();
  const source=browser || coreView.current()?.pcLink?.media?.blitbun;
  const active=['choosing','starting','running','retrying'].includes(source?.phase);
  const unavailable=serverUnavailable || !!state?.preview;
  const status=mediaPhaseLabel(source) || 'Waiting for window selection.';
  tab.status.dataset.state=source?.phase || 'idle';
  setText(tab.status,status+(source?.error?' '+source.error:''));
  const link=coreView.current()?.pcLink;
  const width=link?.blitbunProfile?.width;
  const confirmed=link?.blitbunVideo;
  tab.received.hidden=!confirmed;
  const receivedLabel=confirmed?confirmed.width+' × '+confirmed.height:'';
  if(tab.receivedSize.textContent!==receivedLabel)tab.receivedSize.textContent=receivedLabel;
  tab.receivedFps.hidden=!link?.blitbunFps;
  if(link?.blitbunFps)setText(tab.receivedFps,' · '+link.blitbunFps+' FPS');
  if(width&&!tab.resolutionPending)tab.resolution.value=String(width);
  tab.resolution.disabled=tab.resolutionPending || unavailable;
  tab.choice.disabled=tab.pending || unavailable || tab.choice.options.length < 2;
  tab.refresh.disabled=tab.pending || unavailable;
  tab.start.disabled=tab.pending || unavailable || active || !tab.choice.value;
  tab.shareTab.disabled=tab.pending || unavailable || active || !tab.capture?.available();
  tab.stop.disabled=tab.pending || (!browser && (unavailable || !active && source?.phase!=='failed'));
}

function createBlitBunTab() {
  const panel=element('div','blitbun-tab'),
    heading=element('header','blitbun-heading'),
    title=element('h1','','BlitBun'),
    status=element('p','workspace-status'),
    body=element('section','blitbun-settings'),
    label=element('label','blitbun-window'),
    choice=element('select'),
    profileLabel=element('label','blitbun-window'),
    resolution=element('select'),
    received=element('p','hint blitbun-feedback'),
    receivedSize=element('span'),
    receivedFps=element('span'),
    message=element('p','hint blitbun-feedback'),
    preview=element('div','blitbun-preview'),
    buttons=element('div','blitbun-actions'),
    refresh=element('button','secondary','Refresh'),
    start=element('button','secondary','Share selected window'),
    shareTab=element('button','primary','Share a browser tab'),
    stop=element('button','secondary','Stop PC source');
  title.setAttribute('translate','no');
  title.tabIndex=-1;
  status.setAttribute('role','status');
  message.setAttribute('role','status');
  heading.append(title,element('p','hint','Share a window or game with your console.'),status);
  label.append(element('span','','Window to share'),choice);
  profileLabel.append(element('span','','Stream resolution'),resolution);
  for(const [width,height] of [[640,360],[832,468],[960,540],[1280,720],[1600,900],[1920,1080]]) {
    const option=element('option','',width+' × '+height);
    option.value=String(width);
    resolution.append(option);
  }
  resolution.value='960';
  const placeholder=element('option','','Choose PC window');
  placeholder.value='';
  choice.append(placeholder);
  choice.onchange=updateBlitBunTab;
  refresh.type=start.type=shareTab.type=stop.type='button';
  buttons.append(refresh,start,shareTab,stop);
  const browserHint=element('p','hint blitbun-feedback',
    'Open the page you want to share in another tab of this browser, then choose it in the sharing dialog.');
  browserHint.id='blitbun-browser-hint';
  shareTab.setAttribute('aria-describedby',browserHint.id);
  preview.hidden=true;
  received.hidden=true;
  received.append(element('span','','Switch received'),': ',receivedSize,receivedFps);
  body.append(label,profileLabel,received,buttons,browserHint,message,preview);
  panel.append(heading,body);
  const tab=blitBunTab={panel,title,status,choice,resolution,received,receivedSize,receivedFps,refresh,start,shareTab,stop,pending:false};
  tab.capture=globalThis.IZ2S_BROWSER_CAPTURE?.({key,api,preview,changed:updateBlitBunTab,
    feedback:text=>setText(message,text)});
  shareTab.onclick=()=>{
    setText(message,'');
    void tab.capture.start();
  };
  async function run(action) {
    if(tab.pending) return;
    tab.pending=true;
    setText(message,'');
    updateBlitBunTab();
    try { await action(); }
    catch(error) { setText(message,error.message); }
    finally { tab.pending=false;updateBlitBunTab(); }
  }
  resolution.onchange=async()=>{
    const width=Number(resolution.value);
    tab.resolutionPending=true;
    updateBlitBunTab();
    try {
      await api('/api/iz2s-core/media/blitbun-profile',{width});
      await coreView.refresh(true);
    } catch(error) {setText(message,error.message);}
    finally {tab.resolutionPending=false;updateBlitBunTab();}
  };
  refresh.onclick=()=>run(async()=>{
    const selected=choice.value;
    const {windows}=await api('/api/iz2s-core/media/windows',{});
    choice.replaceChildren(placeholder);
    for (const window of windows) {
      const option=element('option','',window.title);
      option.setAttribute('translate','no');
      option.value=window.id;
      choice.append(option);
    }
    choice.value=[...choice.options].some(option=>option.value===selected)?selected:'';
    if (!windows.length) setText(message,'Open a visible PC window first.');
  });
  start.onclick=()=>run(async()=>{
    await api('/api/iz2s-core/media/start',{id:'blitbun',window:Number(choice.value)});
    await coreView.refresh(true);
  });
  stop.onclick=()=>run(async()=>{
    if(tab.capture?.status())return tab.capture.stop();
    await api('/api/iz2s-core/media/stop',{id:'blitbun'});
    await coreView.refresh(true);
  });
  return panel;
}

function openBlitBunTab() {
  closeTransientMenus();
  workspace.openLocal('blitbun-settings','BlitBun',createBlitBunTab,()=>{void blitBunTab?.capture?.stop();});
  updateBlitBunTab();
  blitBunTab.title.focus({preventScroll:true});
  blitBunTab.refresh.click();
}

function showBlitBunWindowRequest() {
  const request=coreView.current()?.pcLink?.media?.blitbun?.windowRequest;
  if (!request || request===shownBlitBunWindowRequest || !state || state.preview ||
      serverUnavailable || document.hidden ||
      !state.library?.some(app=>app.id==='blitbun')) return;
  shownBlitBunWindowRequest=request;
  openBlitBunTab();
}
document.addEventListener('visibilitychange',showBlitBunWindowRequest);
function leaveLibraryContext(){
  if(!libraryContext)return;const previous=libraryContext;libraryContext=null;
  $('library-context').hidden=true;$('library-search').value=previous.query;$('library-status').value=previous.status;$('library-location').value=previous.location;$('library-format').value=previous.format;
  return previous;
}
function openConsoleApplication(app){
  libraryContext={id:app.id,name:app.name,query:$('library-search').value,status:$('library-status').value,location:$('library-location').value,format:$('library-format').value,scroll:window.scrollY};
  switchView('apps',{focusApp:true});$('library-search').value='';$('library-status').value='all';$('library-location').value='all';$('library-format').value='all';
  $('library-context').hidden=false;$('library-context-name').textContent=app.name;renderLibrary();$('library-back').focus({preventScroll:true});
}
$('library-back').onclick=()=>{
  const previous=libraryContext;
  switchView('setup');
  if(previous){
    window.scrollTo({top:previous.scroll,left:0,behavior:'instant'});
    document.querySelector('#console-apps [data-app-id="'+CSS.escape(previous.id)+'"]')?.focus({preventScroll:true});
  }
};
$('library-context-all').onclick=()=>{leaveLibraryContext();applyLibrarySearch();};
function renderConsoleApps(){
  const box=$('console-apps'),list=$('console-apps-list');if(!box||!list)return;
  const core=coreView.current();
  // PC Link lists modules with a launcher.iza on SD.
  const apps=core?.session?(core.pcLink?.apps||[]).map(row=>{
    const known=state.library?.find(p=>p.id===row.id);
    return {id:row.id,name:known?.name||row.name||row.id,icon:known?.icon||'/icon.svg',sd:true,format:row.format||'.iza'};
  }):globalThis.IZ2S_PRESENTATION.consoleApps(state);
  box.hidden=!apps.length||serverUnavailable||!!state?.preview||!core?.session;
  const stamp=JSON.stringify(apps);if(list.dataset.stamp===stamp)return;list.dataset.stamp=stamp;list.replaceChildren();
  for(const app of apps){
    const item=element('button','console-app'),img=element('img'),copy=element('span','console-app-copy'),name=element('strong','app-name',app.name),places=element('span','console-app-places');
    item.type='button';item.dataset.appId=app.id;item.title=app.name;img.src=app.icon;img.alt='';name.setAttribute('translate','no');
    for(const [available,label]of [[app.ram,'RAM'],[app.sd,'SD']])if(available){const badge=element('small',label==='SD'?'source-sd':'source-ram',label);badge.setAttribute('translate','no');places.append(badge);}
    if(app.format){
      const badge=element('small','console-app-format',app.format);
      badge.setAttribute('translate','no');
      places.append(badge);
    }
    copy.append(name,places);item.append(img,copy);item.onclick=()=>openConsoleApplication(app);list.append(item);
  }
}
$('console-apps-library').onclick=()=>coreView.current()?.session?openCorePcTool('apps'):switchView('apps');
function renderLibrary() {
  const state=storageState();
  const coreSd=state.coreSd?.selected;
  const link=coreView.current()?.pcLink;
  const transfer=$('library-core-transfer');
  transfer.hidden=!state.coreLive||(!link?.queued&&!link?.imported&&!link?.inventoryError);
  if(!transfer.hidden) {
    if(link.queued) {
      const count=element('span','',String(link.queued));count.setAttribute('translate','no');
      transfer.replaceChildren(element('span','','Queued:'),' ',count,' · ',element('span','','Open Add app on Switch if the transfer does not start.'));
    } else if(link.inventoryError) {
      transfer.replaceChildren(element('span','','Could not refresh SD apps:'),' ',element('span','',link.inventoryError));
    } else {
      const name=state.library?.find(pkg=>pkg.id===link.imported.id)?.name||link.imported.id;
      const label=element('span');label.setAttribute('translate','no');label.textContent=name;
      transfer.replaceChildren(element('span','',link.imported.state==='rejected'?'Installation failed:':link.imported.state==='already-present'?'Already installed:':'On SD:'),' ',label);
    }
  }
  const toggle=$('library-core-sd');
  if(toggle){toggle.hidden=!!state.preview;toggle.setAttribute('aria-pressed',String(!!coreSd));}
  const packages=globalThis.IZ2S_PRESENTATION?.library?.(state)||state.library;
  renderIssueList('module-errors', state.moduleErrors);
  const civ=['civswitch','civswitch-ldn','iz2sciv-core'].includes(state.platform?.loaderId);
  const available = new Set(packages.filter(pkg => pkg.supported !== false&&(!civ||pkg.session?.compatible!==false)).map(pkg => pkg.id));
  for (const id of selection) if (!available.has(id)) selection.delete(id);
  const stamp = JSON.stringify(
    [state.platform?.sdLauncher,coreSd,state.coreLive,packages.map((p) => [p.id, p.version, p.bytes, p.name, p.icon, p.description, p.supported, p.contents, p.sd, p.session, p.format])],
  );
  if (stamp !== packageStamp) {
    packageStamp = stamp;
    $('library').replaceChildren();
    if (!packages.length) {
      const empty = element('section', 'empty empty-state');
      empty.append(element('h2', '', 'No modules installed'),
        element('p', '', 'Open the Modules folder to check which modules are installed.'));
      $('library').append(empty);
    }
    $('module-host-actions').dataset.stamp = '';
    for (const pkg of packages) {
      const card = element('article', 'app-card'),
        label = element(state.coreLive?'div':'label', 'app-select'),
        input = element('input'),
        img = element('img');
      card.dataset.appId = pkg.id;label.title=pkg.name;
      input.type = 'checkbox';
      input.disabled = state.coreLive || !available.has(pkg.id);
      input.hidden = !!state.coreLive;
      if(!state.coreLive&&!coreSd&&state.platform?.sdLauncher&&(!pkg.sd?.compatible||pkg.sd?.hostRequired))label.title='Stored on SD; uses the PC executor when launched.';
      if (input.disabled) selection.delete(pkg.id);
      input.checked = selection.has(pkg.id);
      input.setAttribute('aria-label', 'Select ' + pkg.name);
      input.addEventListener('change', () => {
        if (input.checked) selection.add(pkg.id);
        else selection.delete(pkg.id);
        renderControls();
      });
      img.src = pkg.icon;
      img.alt = '';
      label.append(
        ...(state.coreLive?[]:[input]),
        img,
        element('strong', 'app-name', pkg.name),
        element(
          'small',
          '',
          (pkg.sdOnly?'SD: ':'PC: ')+(pkg.version||'version unknown') + ' · ' + (pkg.bytes / 1024).toFixed(1) + ' KiB',
        ),
        element('p', 'app-description', pkg.description),
      );
      const format = globalThis.IZ2S_PRESENTATION.appFormat(pkg);
      if(format){
        const badge=element('small','app-format-badge',format);
        badge.dataset.format=format;
        badge.setAttribute('translate','no');
        label.append(badge);
      }
      const installInfo = element('span', 'app-install-info');
      installInfo.append(element('span', 'app-install-status'), element('small', 'app-sd-version'));
      label.append(installInfo);
      if(civ&&pkg.session?.compatible===false)label.append(element('small','app-requirement',pkg.session.reason||'Unavailable for this game'));
      else if(pkg.session?.requiresFilesystem)label.append(element('small','app-requirement','Requires filesystem access'));
      card.append(label, element('div', 'module-host-actions'));
      if(!state.coreLive&&!coreSd&&state.platform?.sdLauncher&&(!pkg.sd?.compatible||pkg.sd?.hostRequired))card.append(element('p','hint','Stored on SD; uses the PC executor when launched.'));
      if (pkg.contents) {
        const details = element('details', 'module-contents');
        details.append(
          element('summary', '', 'Package contents'),
          element(
            'p',
            '',
            'Switch sources: ' +
              (pkg.contents.switchSourceBytes / 1024).toFixed(1) +
              ' KiB · Server sources: ' +
              (pkg.contents.serverSourceBytes / 1024).toFixed(1) +
              ' KiB. Server code stays on this PC.' +
              (pkg.contents.upstreamSourceBytes
                ? ' Upstream sources: ' +
                  (pkg.contents.upstreamSourceBytes / 1048576).toFixed(1) +
                  ' MiB.'
                : '') +
              (pkg.contents.moduleDependencies?.length
                ? ' Required modules: ' +
                  pkg.contents.moduleDependencies.join(', ') +
                  '.'
                : '') +
              ((pkg.contents.externalDependencies || []).length
                ? ' Downloaded separately: ' +
                  (pkg.contents.externalDependencies || []).join(', ') +
                  '.'
                : ''),
          ),
        );
        details.onclick = (e) => e.stopPropagation();
        card.append(details);
      }
      const actions = element('div', 'module-actions');
      if (!pkg.sdOnly && (pkg.coreSd?.compatible || format === '.nro')) {
        const extension = format === '.nro' ? '.nro' : '.iza';
        const caption = 'Download ' + extension;
        const download = element('button','secondary app-download',caption);
        download.type = 'button';
        download.disabled = serverUnavailable;
        download.onclick = async () => {
          download.dataset.pending = 'true';
          download.disabled = true;
          setText(download,'Preparing download…');
          try {
            const response = await api('/api/apps/download?'+new URLSearchParams({id:pkg.id}),undefined,
              {responseType:'response',timeoutMs:180000});
            const filename = /^attachment; filename="([a-z0-9-]+\.(?:iza|nro))"$/.exec(
              response.headers.get('content-disposition') || '')?.[1];
            if (!filename?.endsWith(extension)) throw Error('The application download is invalid.');
            saveDownload(await response.blob(),filename);
          } catch(error) { notice(error.message); }
          finally {
            delete download.dataset.pending;
            download.disabled = serverUnavailable;
            setText(download,caption);
          }
        };
        actions.append(download);
      }
      if (state.coreLive && !pkg.sdOnly && (pkg.coreSd?.compatible || format === '.nro')) {
        const install = element('button', 'primary module-configure library-install');
        setText(install, 'Install on SD');
        install.type = 'button';
        install.onclick = async () => {
          if (install.disabled) return;
          install.dataset.pending = 'true';
          busy = true;
          renderLibrary();
          try {
            const result = await api('/api/iz2s-core/pc/install-library', {id: pkg.id});
            notice(result.queued ? coreText('openAddAppToInstall') :
              result.alreadyInstalled || result.reused ? coreText('sameVersion') : coreText('imported'), 'success');
            await coreView.refresh(true);
          } catch (error) {
            notice(error.message);
          } finally {
            delete install.dataset.pending;
            busy = false;
            renderLibrary();
          }
        };
        actions.append(install);
      }
      if(pkg.module?.server?.settings){
        const configure=element('button','secondary module-configure',pkg.module.server.settings.label||'Settings');
        configure.dataset.unavailable=String(pkg.supported===false);
        configure.type='button';configure.disabled=serverUnavailable||state.preview===true||pkg.supported===false;
        configure.title='Configure this module before launch; preferences are kept between sessions';
        configure.onclick=async()=>{
          configure.dataset.pending='true';configure.disabled=true;
          try{if(!workspaceReady)throw Error('Reopen IZ2S to enable module tabs.');const value=await api('/api/module/settings',{id:pkg.module.id});workspace.openRequest(value.tab,true);}
          catch(error){notice(error.message);}finally{delete configure.dataset.pending;renderLocalActionAvailability();}
        };
        actions.append(configure);
      }
      const openModule=element('button','secondary','App details');openModule.type='button';openModule.onclick=()=>{try{workspace.openModule(pkg.module?.id||pkg.id);}catch(error){notice(error.message);}};actions.append(openModule);
      const folder=folderButton('module',pkg.module?.id||pkg.id,'Folder');folder.title='Open module folder';folder.setAttribute('aria-label','Open module folder');actions.append(folder);
      if(!state.coreLive&&format!=='.nro')card.append(storageControls.mount(pkg));
      card.append(element('small', 'app-memory'), actions);
      const technical=element('details','app-more'),summary=element('summary','','More'),detailBody=element('div','app-more-body menu-surface');technical.append(summary,detailBody);detailBody.append(element('p','app-full-description',pkg.description));if(pkg.sdOnly){const location=element('code');location.textContent=pkg.displayPath||'SD:/iz2s/modules/'+pkg.id+'/';detailBody.append(location);}
      const detailActions=element('div','app-detail-actions');detailBody.append(detailActions);
      if (pkg.id==='blitbun' && !pkg.sdOnly && !state.preview) {
        const open=element('button','secondary module-configure blitbun-choose-window','BlitBun settings');
        open.type='button';
        open.onclick=openBlitBunTab;
        actions.append(open);
      }
      if (pkg.id==='vacootube' && !pkg.sdOnly && !state.preview) {
        detailBody.append(element('p','hint','Open VacooTube on Switch to start the PC source automatically.'));
        const media=element('div','app-detail-actions');
        for (const [action,label] of [['start','Start video on PC'],['stop','Stop PC source']]) {
          const button=element('button','secondary',label);
          button.type='button';
          button.onclick=async()=>{
            button.disabled=true;
            try {
              await api('/api/iz2s-core/media/'+action,{id:pkg.id});
              if (action==='start') notice('PC source started. Open '+pkg.name+' on Switch.','success');
            } catch(error) { notice(error.message); }
            finally { button.disabled=false; }
          };
          media.append(button);
        }
        const sourceStatus=element('p','hint app-media-status');
        sourceStatus.setAttribute('role','status');
        detailBody.append(sourceStatus,media);
      }
      const cardActions=card.querySelector('.module-actions');if(cardActions){for(const button of [...cardActions.children])if(!button.classList.contains('module-configure'))if(!pkg.sdOnly)detailActions.append(button);else button.remove?.();const contents=card.querySelector('.module-contents');if(contents)detailBody.append(contents);const primaryActions=element('div','app-primary-actions');for(const control of cardActions.querySelectorAll(':scope > .module-configure'))primaryActions.append(control);cardActions.append(primaryActions,technical);}
      for(const child of [...card.children])if(child.tagName==='P'&&child.classList.contains('hint'))detailBody.append(child);
      card.querySelector('.app-select')?.querySelector('strong')?.setAttribute('translate','no');
      $('library').append(card);
    }
  }
  const sdRefresh=$('library-sd-refresh');
  sdRefresh.hidden=civ||!(coreSd||state.platform?.sdLauncher||state.sdStorage?.enabled);
  const sdError=globalThis.IZ2S_STORAGE?.reason?.(state,{id:'sd-inventory'},'sd',{remove:true})||'';
  sdRefresh.disabled=busy||serverUnavailable||(state.coreLive?!state.connected:(!coreSd&&!!sdError));sdRefresh.title=state.coreLive?'':sdError;
  const installed = new Set([...(state.installed||[]),...(state.sdInstalled||[])].map(app => typeof app === 'string' ? app : app.id));
  for (const card of $('library').querySelectorAll('.app-card')) {
    const blitbunControl = card.querySelector('.blitbun-choose-window');
    if (blitbunControl) blitbunControl.disabled = serverUnavailable;
    const sourceStatus=card.querySelector('.app-media-status');
    if(sourceStatus){
      const source=link?.media?.[card.dataset.appId];
      const label=mediaPhaseLabel(source);
      sourceStatus.hidden=!label;
      setText(sourceStatus,label+(source?.error?' '+source.error:''));
    }
    const loaded = installed.has(card.dataset.appId);
    card.classList.toggle('installed', loaded);const presence=globalThis.IZ2S_STORAGE?.installed?.(state,card.dataset.appId)||{session:loaded,sd:false};card.classList.toggle('in-ram',presence.session);card.classList.toggle('on-sd',presence.sd);
    card.dataset.installed = String(loaded);
    const installStatus = card.querySelector('.app-install-status');
    const canCheckInstallation = !!coreSd || (state.coreLive
      ? !!link?.online && !!link?.files && !link?.inventoryError
      : !!state.connected);
    const status = loaded ? 'installed' : canCheckInstallation ? 'missing' : 'unknown';
    card.dataset.installStatus = status;
    setText(installStatus, presence.sd ? 'Installed on SD' : loaded ? 'Installed' :
      status === 'missing' ? 'Not installed' : 'Installation status unavailable');
    const sdVersion=card.querySelector('.app-sd-version'),sdRow=state.sdInstalled?.find(row=>row?.id===card.dataset.appId);
    const install = card.querySelector('.library-install');
    if (install) {
      const pkg = packages.find(item => item.id === card.dataset.appId);
      const samePackage = globalThis.IZ2S_PRESENTATION.sameSdPackage(sdRow,pkg);
      const update = sdRow?.versionVerified && pkg?.version && sdRow.version !== pkg.version;
      install.hidden = !!samePackage;
      install.disabled = busy || serverUnavailable || !state.connected || !link?.files || !!link?.queued;
      const label = !presence.sd ? 'Install on SD' : update ? 'Update on SD' : 'Reinstall on SD';
      setText(install, install.dataset.pending ? 'Preparing installation…' : label);
    }
    sdVersion.hidden=!presence.sd;
    setText(sdVersion,presence.sd?'SD · '+(sdRow?.versionVerified?sdRow.version:'Version unknown'):'');
    card.querySelector('img').title = loaded ? 'Loaded on console' : 'Not loaded on console';
    const retained=state.residentMemory?.apps?.find(app=>app.id===card.dataset.appId),memoryLabel=card.querySelector('.app-memory');
    if(memoryLabel){memoryLabel.hidden=!retained;setText(memoryLabel,retained?(state.connected?'Retained in RAM':'Previously retained')+' · '+(retained.bufferBytes/1048576).toFixed(1)+' MiB':'');}
    storageControls.update(card.querySelector('.app-storage'),packages.find(pkg=>pkg.id===card.dataset.appId));
    if (state.coreLive || (coreSd && sdRow?.format === '.nro')) {
      let remove = card.querySelector('.core-remove');
      if (presence.sd && !remove) {
        remove = element('button', 'quiet core-remove');
        setText(remove, 'Remove from SD');
        remove.type = 'button';
        remove.onclick = async () => {
          const snapshot = storageState();
          const app = snapshot.sdInstalled?.find(row => row?.id === card.dataset.appId);
          if (!app || remove.dataset.pending) return;
          const pkg = packages.find(p => p.id === app.id);
          remove.dataset.pending = 'true';
          remove.disabled = true;
          try {
            const question = app.format === '.nro'
              ? 'Delete the entire folder? This also deletes games, BIOS, settings and saves inside it.'
              : 'Remove this app from the launcher? Games and saves will stay on SD.';
            const target = app.format === '.nro' ? app.displayPath?.replace(/launcher\.nro$/, '') : pkg?.name;
            if (!await confirmRemoval(question, target || pkg?.name)) return;
            if (snapshot.coreSd?.selection !== storageState().coreSd?.selection)
              throw Error('The selected IZ2S SD card changed. Refresh the panel and retry.');
            if (coreSd) await act('/api/unload', {ids: [app.id], target: 'sd'});
            else {
              await api('/api/iz2s-core/pc/remove', {id: app.id, format: app.format});
              await coreView.refresh(true);
            }
          } catch (error) {
            notice(error.message);
          } finally {
            delete remove.dataset.pending;
            renderLibrary();
          }
        };
        card.querySelector('.app-detail-actions').prepend(remove);
      }
      if (remove) {
        remove.hidden = !presence.sd;
        remove.disabled = !!remove.dataset.pending || busy || serverUnavailable || !state.connected;
        remove.title = sdRow?.format === '.nro' && !coreSd
          ? globalThis.RUN21_LANGUAGE?.t('Enable FTP on IZ2S with L + R3 to remove an NRO.') || '' : '';
      }
    }
  }
  if(coreSd)$('module-host-actions').hidden=true;
  else renderModuleHostActions();
  setText($('app-count'), packages.length);
  const libraryConnection = $('library-connection');
  libraryConnection.classList.toggle('online', !!state.connected);
  libraryConnection.title=coreSd ? state.coreSd.target : '';
  const cardName=state.coreSd?.target?.split('/').filter(Boolean).at(-1);
  const connectionLabel = coreSd ? (state.connected ? 'IZ2S SD card: '+cardName : 'IZ2S SD card unavailable') : state.connected ? 'Switch 2 connected' : 'Switch 2 not connected';
  if (libraryConnection.querySelector('span').textContent !== connectionLabel)
    libraryConnection.querySelector('span').textContent = connectionLabel;
  $('load').hidden=!!state.coreLive;
  $('library-batch').hidden=!!state.coreLive;
  $('load').textContent=state.platform?.sdLauncher?'Install selected on SD':'Install / load selected';
  $('library-note').textContent = state.coreLive?'Add a package to install it in SD:/iz2s/modules/.':coreSd ? 'Install or remove applications on the SD card connected to this PC. Games and saves are kept.' : state.platform?.sdLauncher
    ? 'Applications are installed in SD:/iz2s/modules/. The console discovers and opens them locally. Modules requiring PC services or a separate native entry are unavailable in this mode.'
    : state.platform?.residentGraphics && state.active
    ? 'Compatible apps remain in memory for this session. '+(state.residentMemory?'Session memory: '+(state.residentMemory.usedBytes/1048576).toFixed(1)+' / '+(state.residentMemory.budgetBytes/1048576).toFixed(0)+' MiB. ':'')+'Add apps from the menu while the PC is available. Select the full set before a retained-audio session.'
    : state.platform?.variant==='original'&&!civ
    ? 'Choose a launcher in Connection to load applications.'
    : state.connected
    ? 'Choose an installation destination on each card. Selected: ' + selection.size
    : selectedShellName()==='Core only'?'Choose a launcher in Connection to load applications.':'Connect Switch 2 and start ' + selectedShellName() + ' to load apps.';
  renderJobs();
  applyLibrarySearch();
  const url = state.streamUrl;
  try {
    const u = new URL(url);
    if (u.hostname !== '127.0.0.1' || u.protocol !== 'http:') throw Error();
    $('stream-link').href = url;
    $('stream-link').hidden = !state.connected;
    $('stream-link').onclick=async event=>{event.preventDefault();try{if(!workspaceReady)throw Error('Reopen IZ2S to enable module tabs.');const result=await api('/api/workspace/attach',{url});workspace.openRequest(result.tab,true);}catch(error){notice(error.message);}};
  } catch {
    $('stream-link').hidden = true;
  }
}
function renderModuleHostActions() {
  const hostList = $('module-host-actions'),
    targets = [...document.querySelectorAll('.module-host-actions')],
    existing = new Map(targets.flatMap(target => [...target.children].map(row => [row.dataset.hostActionKey, row]))),
    desired = new Map(targets.map(target => [target, []])),
    cards = new Map([...$('library').children].map(card => [card.dataset.appId, card]));
  for (const module of state.moduleCatalog || []) {
    const target = cards.get(module.id)?.querySelector('.module-host-actions') || hostList;
    for (const pkg of module.server?.localPackages || []) {
      const record = state.hostModules?.[pkg.id];
      if (record?.installed && module.id!=='vacootube') continue;
      const key = 'runtime:' + module.id + ':' + pkg.id;
      let box = existing.get(key);
      if (!box) {
        box = element('div', 'local-module module-setup'); box.dataset.hostActionKey = key;
        const text = element('div', 'module-setup-body');
        text.append(element('p', 'hint'));
        box.append(element('strong', '', 'PC setup'), text, element('button', 'secondary'));
      }
      const text = box.querySelector('.module-setup-body'), button = box.querySelector('button');
      setText(text.querySelector('.hint'), record?.installed ? 'Installed on this PC' : pkg.importSource
        ? 'Choose the local game folder. Saved games stay in the original installation.'
        : record?.missingFiles?.length ? 'Local runtime files are missing. Reinstall this component.'
        : 'This component is downloaded separately.');
      let input = box.querySelector('.local-source');
      if (pkg.importSource) {
        if (!input) {
          input = element('input', 'local-source'); input.type = 'text'; input.dataset.runtime = pkg.id;
          input.placeholder = 'Absolute runtime folder'; text.append(input);
        }
        input.setAttribute('aria-label', module.name + ' runtime folder');
        input.oninput = () => {
          button.disabled = serverUnavailable || busy || state.preview || state.hostModules?.[pkg.id]?.supported === false || !input.value.trim();
        };
      } else if (input) { input.remove(); input = null; }
      button.type = 'button';
      setText(button, record?.installed ? 'Reinstall on this PC' : pkg.importSource ? 'Import local runtime' : 'Install on this PC');
      button.dataset.unavailable = String(record?.supported === false);
      button.disabled = serverUnavailable || busy || state.preview || record?.supported === false || (!!input && !input.value.trim());
      button.onclick = () => act('/api/install-host', {id:pkg.id, ...(input ? {source:input.value.trim()} : {})});
      desired.get(target).push(box);
    }
    if (module.server && !module.switch) {
      const key = 'host:' + module.id;
      let box = existing.get(key);
      if (!box) {
        box = element('div', 'local-module'); box.dataset.hostActionKey = key;
        box.append(element('strong'), element('button', 'secondary'));
      }
      const button = box.querySelector('button'), running = state.moduleStates?.some(item => item.id === module.id);
      setText(box.querySelector('strong'), module.name);
      setText(button, running ? 'Stop' : 'Start');
      button.type = 'button'; button.disabled = serverUnavailable || busy || state.preview;
      button.onclick = () => act(running ? '/api/module/stop' : '/api/module/start', {id:module.id});
      desired.get(target).push(box);
    }
  }
  const retained = new Set([...desired.values()].flat());
  for (const target of targets) {
    for (const row of [...target.children]) if (!retained.has(row)) row.remove();
    desired.get(target).forEach((row, index) => placeChild(target, row, index));
    target.hidden = !target.childElementCount;
  }
}
function renderTransferStatus(){
 const box=$('transfer-status');if(!box)return;const item=globalThis.IZ2S_PRESENTATION.transfer(state);box.hidden=!item;if(!item)return;
 setText($('transfer-name'),item.name);setText($('transfer-target'),item.target);setText($('transfer-phase'),item.label);setText($('transfer-count'),item.count>1?'+'+(item.count-1):'');
 const progress=$('transfer-progress');if(item.percent===null)progress.removeAttribute('value');else progress.value=item.percent;
 setText($('transfer-bytes'),item.total===null?'':item.percent+'% · '+(item.completed/1048576).toFixed(1)+' / '+(item.total/1048576).toFixed(1)+' MiB');
}
let jobsStamp='';
function renderJobs() {
  const stamp=JSON.stringify(state.jobs);if(stamp===jobsStamp)return;jobsStamp=stamp;
  const active=$('jobs'),completed=$('completed-job-list');active.replaceChildren();completed.replaceChildren();
  for(const job of [...state.jobs].sort((a,b)=>{const pa=globalThis.IZ2S_PRESENTATION.job(a),pb=globalThis.IZ2S_PRESENTATION.job(b);return Number(pb.active||pb.failed)-Number(pa.active||pa.failed)||b.created.localeCompare(a.created);}).slice(0,24)){
    const info=globalThis.IZ2S_PRESENTATION.job(job),row=element('article','job '+job.status);row.dataset.jobId=job.id;
    const name=element('strong','',job.name);name.setAttribute('translate','no');row.append(name,element('span','job-status',info.label));
    if(info.active){const progress=element('progress');progress.max=100;progress.setAttribute('aria-label','Progress: '+job.name);if(Number.isFinite(job.progress?.percent))progress.value=job.progress.percent;
      const labels={verify:'Checking files',prepare:'Preparing on PC',graphics:'Preparing graphics',transfer:'Updating console menu','sd-prepare':'Preparing SD application','sd-write':'Writing to SD','sd-verify':'Verifying SD','sd-publish':'Waiting for the SD menu'};
      const p=job.progress,bytes=p?.totalBytes>0?' · '+(p.completedBytes/1048576).toFixed(1)+' / '+(p.totalBytes/1048576).toFixed(1)+' MiB':'';
      row.append(progress,element('small','job-detail',(labels[p?.phase]||info.label)+bytes));
    }
    if(info.failed)appendProblem(row,job.error||'The operation failed.');
    (info.active||info.failed?active:completed).append(row);
  }
  $('completed-jobs').hidden=!completed.children.length;
}
function appendProblem(container,message){
  const problem=globalThis.IZ2S_ERRORS.describe(message),box=element('div','operation-problem panel-message');box.dataset.tone='error';box.setAttribute('role','status');
  box.append(element('strong','problem-title',problem.title));
  if (problem.action) box.append(element('p','hint',problem.action));
  if (problem.detail && problem.detail.trim() !== problem.title) {
    const details=element('details','technical-details'),pre=element('pre','',problem.detail);
    pre.setAttribute('translate','no');details.append(element('summary','','Technical details'),pre);box.append(details);
  }
  container.append(box);
}
const sessionActionHomes=new Map();

let configurationOpen=false,libraryBatch=false,historyLimit=10;
function renderSessionPresentation(){
 $('apps').classList.toggle('batch-selection',libraryBatch);
 $('library-batch').setAttribute('aria-pressed',String(libraryBatch));
 setText($('library-batch'),libraryBatch?'Finish selection':'Select multiple');
 document.querySelector('.library-bar').hidden=!libraryBatch;applyLibrarySearch();
}
function applyLibrarySearch(){
  const status=$('library-status').value,location=$('library-location').value,format=$('library-format').value,query=$('library-search').value;
  const result=globalThis.IZ2S_PRESENTATION.libraryFilter(storageState(),{query,status,location,format,focusId:libraryContext?.id});const ids=new Set(result.ids);
  for(const card of $('library').children)card.hidden=!!card.dataset.appId&&!ids.has(card.dataset.appId);
  const filtered=!!query.trim()||status!=='all'||location!=='all'||format!=='all';$('library-reset').hidden=!filtered;
  $('library-result-count').textContent=result.ids.length+' / '+result.total;
  $('library-empty').hidden=result.ids.length>0||![...$('library').children].some(card=>card.dataset.appId);
  const hidden=[...selection].filter(id=>!ids.has(id)).length;$('library-hidden-selection').hidden=!libraryBatch||!hidden;
  $('library-hidden-selection').textContent=hidden?'Selected outside this filter: '+hidden:'';
}
function resetLibraryFilters(){$('library-search').value='';$('library-status').value='all';$('library-location').value='all';$('library-format').value='all';applyLibrarySearch();}
$('library-reset').onclick=resetLibraryFilters;$('library-empty-reset').onclick=resetLibraryFilters;
$('library-status').onchange=()=>{if($('library-status').value==='all')$('library-location').value='all';applyLibrarySearch();};
$('library-location').onchange=()=>{if($('library-location').value!=='all'&&$('library-status').value==='all')$('library-status').value='installed';applyLibrarySearch();};

;
;
$('library-search').oninput=applyLibrarySearch;
$('library-format').onchange=applyLibrarySearch;
$('library-batch').onclick=()=>{libraryBatch=!libraryBatch;if(!libraryBatch){selection.clear();for(const input of document.querySelectorAll('.app-select input'))input.checked=false;}renderSessionPresentation();renderControls();};
function refreshHistoryFilters(){historyLimit=10;historyStamp='';renderHistory();}
const historyShortcuts=[['history-today-all','all'],['history-today-success','successful'],['history-today-errors','unsuccessful'],['history-today-pending','pending']];
$('history-search').oninput=refreshHistoryFilters;
for(const id of ['history-filter','history-period','history-error-type'])$(id).onchange=()=>{
  if(id==='history-error-type'&&$(id).value!=='all')$('history-filter').value='unsuccessful';
  if(id==='history-filter'&&$(id).value!=='unsuccessful')$('history-error-type').value='all';
  refreshHistoryFilters();
};
for(const [id,kind] of historyShortcuts)$(id).onclick=()=>{
  $('history-period').value='today';$('history-filter').value=kind;$('history-error-type').value='all';refreshHistoryFilters();
};
$('history-empty-action').onclick=()=>{
  $('history-search').value='';
  $('history-period').value='all';
  $('history-filter').value='all';
  $('history-error-type').value='all';
  refreshHistoryFilters();
  $('history-search').focus({preventScroll:true});
};
$('history-more').onclick=()=>{historyLimit+=10;historyStamp='';renderHistory();};

let historyStamp = '';
let savedLogAttempt = null;
let savedLogGeneration = 0;
let savedLogOrigin = null;
function logTime(value) {
  return new Date(value).toLocaleTimeString(globalThis.RUN21_LANGUAGE?.locale || 'en-GB', {
    hour12: false, hour: '2-digit', minute: '2-digit', second: '2-digit',
    fractionalSecondDigits: 3,
  });
}
function logMessage(event) {
  event={...event,message:globalThis.IZ2S_ERRORS.canonical(event.message)};
  const owner = [event.source, event.stream].filter(Boolean).join('/');
  const elapsed = Number.isFinite(event.elapsedMs) ? ` +${event.elapsedMs}ms` : '';
  const pid = event.pid ? ` pid=${event.pid}` : '';
  return (owner ? `[${owner}${pid}${elapsed}] ` : '') + event.message;
}
async function downloadSessionLog(id, onError = error => notice(error.message)) {
  try {
    const value = await api('/api/log?id=' + encodeURIComponent(id), undefined, {timeoutMs:15000});
    if(value.source==='iz2s-core'){
      const blob=await api('/api/iz2s-core/log?id='+encodeURIComponent(id),undefined,{responseType:'blob',timeoutMs:30000});
      saveDownload(blob,'IZ2S-core-'+id+'.log');return;
    }
    const text = value.events.map((e) => e.at + '  ' + logMessage(e)).join('\n');
    saveDownload(new Blob([text + '\n'], {type:'text/plain;charset=utf-8'}), 'IZ2S-session-' + id + '.log');
  } catch (e) { onError(e); }
}
function showSavedLogError(error, title, next, retry) {
  const problem = globalThis.IZ2S_ERRORS.describe(error);
  setText($('saved-log-error-title'), title);
  setText($('saved-log-error-note'), error.statusCode === 403 ? problem.action : next);
  setText($('saved-log-diagnostic'), problem.detail);
  $('saved-log-details').open = false;
  $('saved-log-error').hidden = false;
  $('saved-log-retry').hidden = error.statusCode === 403;
  $('saved-log-retry').onclick = retry;
}
function invalidateSavedLog() {
  savedLogGeneration++;
  savedLogAttempt = null;
  $('download-saved-log').disabled = true;
  $('saved-log-error').hidden = true;
  $('saved-log-retry').onclick = null;
  $('log-dialog').setAttribute('aria-busy', 'false');
}
$('log-dialog').addEventListener('close', () => {
  if ($('log-dialog').open) return;
  invalidateSavedLog();
  if (savedLogOrigin && homeView === 'history' && workspaceView.id === 'home')
    (document.querySelector('#attempts [data-attempt-id="' + CSS.escape(savedLogOrigin) + '"] .attempt-log') ||
      $('page-history-title')).focus({preventScroll:true});
  savedLogOrigin = null;
});
$('log-dialog').addEventListener('cancel', invalidateSavedLog);
async function openSavedLog(attempt, fromRetry = false) {
  const generation = ++savedLogGeneration,
    dialog = $('log-dialog'),
    download = $('download-saved-log'),
    created = date(attempt.created);
  savedLogAttempt = null;
  download.disabled = true;
  $('saved-log-error').hidden = true;
  $('saved-log').hidden = false;
  setText($('saved-log-title'), 'Session log' + (created ? ' · ' + created : ''));
  setText($('saved-log-attempt'),
    (attempt.kind === 'bootstrap' ? 'Bootstrap installation' : 'Launcher session') + ' · ' + attempt.id);
  $('saved-log').textContent = globalThis.RUN21_LANGUAGE?.t('Loading saved log…') || 'Loading saved log…';
  dialog.setAttribute('aria-busy', 'true');
  if (!dialog.open) {
    savedLogOrigin = attempt.id;
    dialog.showModal();
  }
  try {
    const value = await api('/api/log?id=' + encodeURIComponent(attempt.id), undefined, {timeoutMs:15000});
    if (generation !== savedLogGeneration || !dialog.open) return;
    const prefix=value.truncated?(globalThis.RUN21_LANGUAGE?.t('Earlier log lines are omitted here. Download the full log.')||'Earlier log lines are omitted here. Download the full log.')+'\n\n':'';
    const text = prefix+value.events.map((event) => event.at + '  ' + logMessage(event)).join('\n');
    $('saved-log').textContent = text || globalThis.RUN21_LANGUAGE?.t('No further details were recorded.') || 'No further details were recorded.';
    savedLogAttempt = attempt.id;
    download.disabled = false;
    if (fromRetry) download.focus();
  } catch (error) {
    if (generation !== savedLogGeneration || !dialog.open) return;
    $('saved-log').hidden = true;
    showSavedLogError(error, 'Could not load this log.', 'Try again or close this window to choose another attempt.', () => openSavedLog(attempt, true));
    if (fromRetry) $('saved-log-retry').focus();
  } finally {
    if (generation === savedLogGeneration && dialog.open) dialog.setAttribute('aria-busy', 'false');
  }
}
function historyRows(){
 const rows=new Map((state.history||[]).map(row=>[row.id,row]));
 for(const row of [state.lastAttempt,state.active])if(row?.id)rows.set(row.id,row);
 return [...rows.values()].sort((a,b)=>String(b.created||'').localeCompare(String(a.created||'')));
}
let historyClearing=false;
function confirmRemoval(message,name=''){
 const dialog=$('removal-dialog');if(dialog.open)return Promise.resolve(false);
 setText($('removal-message'),message);$('removal-name').hidden=!name;setText($('removal-name'),name);dialog.returnValue='';
 return new Promise(resolve=>{dialog.addEventListener('close',()=>resolve(dialog.returnValue==='delete'),{once:true});dialog.showModal();});
}
async function deleteAttemptLogs(ids){
 if(historyClearing||!ids.length)return;
 const question=ids.length===1?'Delete this attempt and its full log?':'Clear all completed attempt logs? The active session and installed apps will stay.';
 if(!await confirmRemoval(question))return;
 historyClearing=true;historyStamp='';renderHistory();
 try{for(const id of ids)await api('/api/delete',{id});}
 catch(error){notice(error.message);}
 finally{historyClearing=false;historyStamp='';await poll();}
}
$('history-clear').onclick=()=>deleteAttemptLogs(historyRows().filter(row=>globalThis.IZ2S_PRESENTATION.attempt(row,{activeId:state.active?.id}).deletable).map(row=>row.id));
if($('library-core-sd')){
 const region=$('library-core-volumes'),volume=$('library-core-volume'),use=$('library-core-use'),
   trigger=$('library-core-sd'),scan=$('library-core-scan'),detach=$('library-core-detach');
 let scanGeneration=0;
 function toggleVolumes(open){
  region.hidden=!open;trigger.setAttribute('aria-expanded',String(open));
  if(open){setText(detach,state?.coreSd?.selected?'Back to console session':'Close');void scanVolumes();}
  else {scanGeneration++;trigger.focus();}
 }
 async function scanVolumes(){
  const generation=++scanGeneration, current=()=>generation===scanGeneration&&!region.hidden;
  use.disabled=volume.disabled=scan.disabled=true;
  setFeedback('library-core-help','Checking mounted SD cards…');
  try{
   const volumes=await api('/api/iz2s-core/sd/volumes',undefined,{timeoutMs:6000});
   if(!current())return;
   volume.replaceChildren(...volumes.map(row=>{const option=element('option','',row.target);option.value=row.target;return option;}));
   if(state?.coreSd?.target&&volumes.some(row=>row.target===state.coreSd.target))volume.value=state.coreSd.target;
   use.disabled=volume.disabled=!volumes.length;
   setFeedback('library-core-help',volumes.length?'Select the Switch SD card you want to manage.':'Mount the Switch SD card on this PC, then select Refresh SD cards.',volumes.length?'info':'warning');
  }catch(error){
   if(current()){
    setFeedback('library-core-help','Could not check mounted SD cards. Select Refresh SD cards to try again.','error');
    notice(error.message);
   }
  }finally{if(current())scan.disabled=false;}
 }
 trigger.onclick=()=>toggleVolumes(region.hidden);
 scan.onclick=scanVolumes;
 use.onclick=async()=>{if(await act('/api/iz2s-core/sd/select',{target:volume.value}))toggleVolumes(false);};
 detach.onclick=async()=>{if(!state?.coreSd?.selected||await act('/api/iz2s-core/sd/select',{target:null}))toggleVolumes(false);};
 const add=$('library-core-add'),input=$('library-core-file');
 add.onclick=()=>{
  if(state?.coreSd?.ready||(!state?.coreSd?.selected&&coreView.current()?.pcLink?.online))input.click();
  else if(region.hidden)$('library-core-sd').click();
 };
 input.onchange=async()=>{
  const file=input.files[0],selection=state?.coreSd?.selection;
  input.value='';
  const live=!state?.coreSd?.selected&&coreView.current()?.pcLink?.online;
  if(!file||(!selection&&!live)||busy)return;
  const format=file.name.split('.').at(-1).toLowerCase();
  if(format!=='iza'){notice(coreText('importFormat'));return;}
  busy=true;renderControls();
  try{
   const route=!live?'/api/iz2s-core/sd/import?'+new URLSearchParams({format,selection,name:file.name}):
     '/api/iz2s-core/pc/install';
   const value=await api(route,file,{upload:true,timeoutMs:180000});
   notice(value.queued?'Queued: '+value.queued:(value.reused||value.alreadyInstalled)?coreText('sameVersion'):coreText('imported'),'success');
   if(live)await coreView.refresh(true);
   else await poll();
  }catch(error){notice(error.message);}
  finally{busy=false;renderControls();}
 };
}
$('library-sd-refresh').onclick=async()=>{if(!storageState()?.coreLive)return act('/api/sd/refresh',{});try{await api('/api/iz2s-core/pc/refresh',{});await coreView.refresh(true);}catch(error){notice(error.message);}};
function renderHistory() {
 const rows=historyRows(),options={query:$('history-search').value,kind:$('history-filter').value,category:$('history-error-type').value||'all',period:$('history-period').value||'today',limit:historyLimit},stamp=JSON.stringify([rows.map(row=>[row.id,row.created,row.kind,row.status,row.error,row.connectedAt]),options,historyClearing,serverUnavailable,state.active?.id,state.historyAll,new Date().toDateString()]);
 setText($('fail-count'),rows.length);$('history-scope-note').hidden=state.historyAll===true;if(stamp===historyStamp)return;historyStamp=stamp;
 const presentation=globalThis.IZ2S_PRESENTATION,today=presentation.statistics(rows);
 setText($('history-today-total'),today.total);setText($('history-today-success-count'),today.successful);setText($('history-today-error-count'),today.unsuccessful);setText($('history-today-pending-count'),today.pending);
 const typeSelect=$('history-error-type');if(typeSelect.children.length===1)for(const [key,name]of Object.entries(presentation.errorLabels)){const option=element('option','',name);option.value=key;typeSelect.append(option);}
 $('history-error-types').replaceChildren();for(const [key,count]of Object.entries(today.errors).sort((a,b)=>b[1]-a[1])){const button=element('button','quiet');button.append(element('span','',presentation.errorLabels[key]),element('strong','',String(count)));button.setAttribute('aria-pressed',String(options.category===key));button.onclick=()=>{$('history-period').value='today';$('history-filter').value='unsuccessful';$('history-error-type').value=key;historyLimit=10;historyStamp='';renderHistory();};$('history-error-types').append(button);}
 if(!today.unsuccessful)$('history-error-types').append(element('span','hint','No errors today'));
 const selected=presentation.history(rows,options);$('attempts').replaceChildren();setText($('history-count'),historyClearing?'Clearing history…':selected.rows.length+' / '+selected.total);$('history-more').hidden=!selected.more;
 $('history-clear').disabled=serverUnavailable||historyClearing||!rows.some(row=>globalThis.IZ2S_PRESENTATION.attempt(row,{activeId:state.active?.id}).deletable);
 for(const [id,kind] of historyShortcuts) $(id).setAttribute('aria-pressed',String(options.period==='today'&&options.kind===kind&&options.category==='all'));
 $('history-empty').hidden=!!selected.rows.length;
 if (!selected.rows.length) {
   const filtered=!!options.query.trim()||options.category!=='all'||options.kind!=='all';
   setText($('history-empty-title'),!rows.length?'No attempts yet':filtered?'No matching attempts':'No attempts today');
   setText($('history-empty-note'),!rows.length?'Connection attempts and their logs will appear here.':filtered?'Change the search or clear the filters.':'You can still review earlier attempts.');
   setText($('history-empty-action'),filtered?'Clear filters':'Show all attempts');
   $('history-empty-action').hidden=!rows.length;
   $('history-empty-connection').hidden=!!rows.length;
 }
 const groups=new Map();
 for(const key of ['successful','unsuccessful','pending']){const groupRows=selected.rows.filter(row=>presentation.attempt(row).outcome===key);if(!groupRows.length)continue;const group=element('section','attempt-group');group.append(element('h2','attempt-group-title',{successful:'Successful',unsuccessful:'Unsuccessful',pending:'Not finished'}[key]));$('attempts').append(group);groups.set(key,group);}
 for(const row of selected.rows){
  const info=globalThis.IZ2S_PRESENTATION.attempt(row,{activeId:state.active?.id}),card=element('article','attempt attempt-compact'),badge=element('span','attempt-status '+info.tone,info.label),body=element('div','attempt-copy'),heading=element('div','attempt-heading');
  card.dataset.attemptId=row.id;
  heading.append(element('strong','',row.kind==='bootstrap'?'First Setup':'Launcher session'),element('time','',date(row.created)));body.append(heading);
  if(row.error){const detail=element('p','attempt-detail');detail.setAttribute('translate','no');detail.textContent=globalThis.IZ2S_ERRORS.describe(row.error).title;detail.title=globalThis.IZ2S_ERRORS.canonical(row.error);body.append(detail);}
  else body.append(element('p','attempt-detail hint',info.success?'Launcher started successfully.':info.label));
  const actions=element('div','attempt-actions'),view=element('button','attempt-log','View log');view.onclick=()=>openSavedLog(row);actions.append(view);
  const remove=element('button','quiet danger','Delete log');remove.disabled=serverUnavailable||historyClearing||!info.deletable;remove.title=info.deletable?'Delete this attempt and its full log.':'The active attempt cannot be deleted.';remove.onclick=()=>deleteAttemptLogs([row.id]);actions.append(remove);
  card.append(badge,body,actions);groups.get(info.outcome).append(card);
 }
}

let iz2scivLogFull = false;
function renderActivity() {
  const civ = state.platform?.loaderId === 'iz2sciv-core';
  $('iz2sciv-log-modes').hidden = !civ;
  $('iz2sciv-log-note').hidden = !civ;
  $('log-details-control').hidden = civ;
  setText($('download-session-log'),civ?'Download attempt log':'Download full log');
  for (const [id,selected] of [['events',!iz2scivLogFull],['output',iz2scivLogFull]]) {
    $('iz2sciv-log-'+id).setAttribute('aria-pressed',String(selected));
    $('iz2sciv-log-'+id).classList.toggle('selected',selected);
  }
  const list = (civ ? iz2scivUI.activityLog(state,iz2scivLogFull) : state.log.filter((e) => $('log-details').checked || e.type !== 'detail')).slice(-250),
    sessionActive = !!state.active || ['starting','running','offline','stopping'].includes(state.civLauncher?.state),
    stamp = JSON.stringify([list, sessionActive, civ, iz2scivLogFull, globalThis.RUN21_LANGUAGE?.locale]),
    log = $('log');
  $('download-session-log').disabled = !(state.active || state.lastAttempt)?.id;
  if (log.dataset.stamp === stamp) return;
  logWriting = true;
  log.dataset.stamp = stamp;
  const countFormat = globalThis.RUN21_LANGUAGE?.t('Events: {count}') || 'Events: {count}';
  $('log-count').textContent = countFormat.replace('{count}', list.length);
  const existing = new Map([...log.children].map(row => [row.dataset.logKey, row])), occurrences = new Map();
  const entries = list.map(item => {
    const signature = JSON.stringify(item), ordinal = occurrences.get(signature) || 0;
    occurrences.set(signature, ordinal + 1);
    return {item, key:signature + ':' + ordinal};
  });
  const retained = new Set(entries.map(entry => entry.key));
  if (!list.length) retained.add('empty');
  for (const [key, row] of existing) if (!retained.has(key)) row.remove();
  entries.forEach(({item, key}, index) => {
    let row = existing.get(key);
    if (!row) {
      row = element('div', 'log-row ' + item.type); row.dataset.logKey = key;
      const message=element('span');message.setAttribute('translate','no');message.textContent=civ&&!iz2scivLogFull?item.message:logMessage(item);
      row.append(element('time', '', logTime(item.at)),message);
    }
    placeChild(log, row, index);
  });
  if (!list.length) {
    let empty=existing.get('empty');
    if(!empty){empty=element('p','hint');empty.dataset.logKey='empty';placeChild(log,empty,0);}
    setText(empty,civ&&!iz2scivLogFull?'No key events yet. Open Full output to see technical messages.':sessionActive?'Waiting for session messages.':'No active attempt. Device checks do not send console commands.');
  }
  requestAnimationFrame(() => {
    if (logFollow && log.clientHeight) log.scrollTop = log.scrollHeight;
    logWriting = false;
  });
}

function renderConnectionStatus(){
 const core=coreView.current();
 const online=!serverUnavailable&&!state?.preview&&((!!core?.pcLink?.online&&!core?.refreshError)||(!!ftpConnection?.online&&!ftpConnection?.refreshError));
 const status=serverUnavailable?'Server unavailable':state?.preview?'Design preview':online?'Console connected':state?.iz2sciv?.busy?'Connecting…':'Not connected';
 $('connection').className='connection '+(online?'online':'disconnected');
 $('connection').lastChild.textContent=globalThis.RUN21_LANGUAGE?.t(status)||status;
 document.querySelector('.nav-dot').hidden=!online;
}
function renderLocalActionAvailability() {
  for (const control of document.querySelectorAll('.folder-button,[data-folder],.module-configure'))
    control.disabled = serverUnavailable || !!state?.preview || control.dataset.pending === 'true' || control.dataset.unavailable === 'true';
  for (const control of document.querySelectorAll('.app-download'))
    control.disabled = serverUnavailable || control.dataset.pending === 'true';
}
function renderServerAvailability(){
 workspace.setAvailable(!serverUnavailable);renderLocalActionAvailability();
 $('panel-main').classList.toggle('server-unavailable',serverUnavailable);
 renderConnectionStatus();
 if(!serverUnavailable)return;
 iz2scivUI.render(state);
 document.querySelectorAll('#load,.module-host-actions button,.module-unload,.module-install,.sd-inventory-refresh,.module-configure,.folder-button,[data-folder],#library-sd-refresh').forEach(e=>e.disabled=true);
}
function render(){
 renderTransferStatus();workspace.updateModules(state);renderPlatform();
 renderLibrary();renderConsoleApps();renderHistory();renderActivity();renderControls();renderConnectionStatus();renderSessionPresentation();
 if(state.notification){backendNotice=state.notification.at||state.notification.message;notice(state.notification.message,state.notification.type,backendNotice);}
 else if(backendNotice){if(notificationKey===backendNotice)$('toast').hidden=true;backendNotice='';}
}

let platformMenuStamp = '',
  configurationStamp = '';
const upstreamSteps = { izmysh: 'launch' };

function renderPlatform(){
 if(!state?.platform)return;
 coreView.render(true,busy,state);
 $('footer-shell').textContent='Civilization VII';
 renderIssueList('extension-errors',state.platform.extensionErrors||[]);
}

document
  .querySelectorAll('[data-loader]')
  .forEach(
    (button) =>
      (button.onclick = () =>
        act('/api/launch-settings', { loaderId: button.dataset.loader })),
  );
;
;
;
document
  .querySelectorAll('[data-variant]')
  .forEach(
    (button) =>
      (button.onclick = () =>
        act('/api/launch-settings', { variant: button.dataset.variant })),
  );
;

let polling = null;
let initialRenderComplete = false;
let dolphinOpening = false;
let dolphinMessage = '';

if ($('dolphin-open')) ;
let fsRpcLogProof = {id: null, state: null, checked: 0};

function showStartupMessage(message, {error, tone = 'warning', retry = true} = {}) {
  $('panel-startup').dataset.tone = tone;
  setText($('startup-message'), message);
  $('startup-retry').hidden = !retry;
  $('startup-retry').disabled = false;
  $('startup-details').hidden = !error;
  if (error) setText($('startup-diagnostic'), globalThis.IZ2S_ERRORS.canonical(error));
}
function showPanelStatus(title, note, tone = 'error', retry = true) {
  const box = $('panel-status');
  box.dataset.tone = tone;
  setText($('panel-status-title'), title);
  setText($('panel-status-note'), note);
  $('panel-status-retry').hidden = !retry;
  box.hidden = !title;
}
function poll(fresh = false) {
  const generation = actionGeneration;
  if (!fresh && polling?.generation === generation) return polling;
  if (!initialRenderComplete) {
    $('startup-retry').disabled = true;
    if (!$('panel-startup').dataset.tone)
      setText($('startup-message'), 'Preparing the app library. The panel will open automatically.');
  }
  const pending = (async () => {
    let readingState = true;
    const last = state;
    const current = () => generation === actionGeneration && polling === pending;
    try {
      const snapshot = await api(initialRenderComplete ? '/api/state' : '/api/startup', undefined,
        {timeoutMs: initialRenderComplete ? 15000 : 120000});
      if (!current()) return;
      readingState = false;
      const next = RUN21_CATALOG.normalizeSnapshot(snapshot),
        civ = ['civswitch','civswitch-ldn','iz2sciv-core'].includes(next.platform?.loaderId),
        legacyCiv = ['civswitch','civswitch-ldn'].includes(next.platform?.loaderId);
      let tabs = legacyCiv ? [] : null;
      if (!civ) {
        await readFsRpcProof(next);
        if (!current()) return;
      }
      const requestedWorkspaceRevision = workspaceRevision;
      if (!legacyCiv) {
        try {
          const listing = await api('/api/workspace', undefined, {timeoutMs:5000});
          if (Array.isArray(listing.tabs)) tabs = listing.tabs;
        }
        catch { /* Keep existing tabs available to read while the workspace refreshes. */ }
      }
      if (!current()) return;
      state = next;
      workspaceReady = !legacyCiv && !!tabs;
      const syncWorkspace = tabs && requestedWorkspaceRevision === workspaceRevision;
      const invalidTools = syncWorkspace ? workspace.sync(tabs) : 0;
      if (invalidTools) notice('Some tool tabs could not be restored. Open the tool again from App library.', 'warning', 'workspace-invalid');
      else if (syncWorkspace) {
        if (notificationKey === 'workspace-invalid') $('toast').hidden = true;
        if (dismissed === 'workspace-invalid') dismissed = '';
      }
      serverUnavailable = false;
      render();
      ;
      $('panel-status').hidden = true;
      if (!initialRenderComplete) {
        restoreNavigation(false);
        initialRenderComplete = true;
        $('panel-sidebar').hidden = false;
        $('panel-main').hidden = false;
        $('panel-startup').hidden = true;
      }
    } catch (e) {
      if (!current()) return;
      if (!readingState) {
        state = last;
        console.error('IZ2S panel rendering failed', e);
        const title = 'The panel could not display the server response.';
        if (!initialRenderComplete) showStartupMessage(title + ' Try again or reopen IZ2S.', {error:e, tone:'error'});
        else {
          serverUnavailable = true;
          renderServerAvailability();
          showPanelStatus(title, 'Try again or reopen IZ2S.');
        }
        return;
      }
      const delayed = e.name === 'AbortError' || e.statusCode === 503;
      const invalidLink = e.statusCode === 403;
      if (!initialRenderComplete && delayed) {
        serverUnavailable = false;
        showStartupMessage('Preparation is taking longer than usual. The panel will retry automatically.');
        return;
      }
      if (!initialRenderComplete && e.statusCode === 400) {
        serverUnavailable = false;
        showStartupMessage('The app library could not be prepared. Try again.', {error:e, tone:'error'});
        console.error('IZ2S startup preparation failed', e);
        return;
      }
      serverUnavailable = true;
      if (!initialRenderComplete) {
        showStartupMessage(invalidLink ? 'This window link is no longer valid. Open 0.34 from its shortcut.' :
          'The local server is unavailable. Try again or reopen IZ2S.', {tone:'error', retry:!invalidLink});
        return;
      }
      renderServerAvailability();
      if (invalidLink) showPanelStatus('This window link is no longer valid.', 'Open 0.34 from its shortcut.', 'error', false);
      else showPanelStatus(delayed ? 'Status update delayed' : 'Local server unavailable',
        delayed ? 'Showing the last known state. The panel will retry automatically.' :
          'Reopen IZ2S Platform from its shortcut to start the local server. This page will reconnect automatically.', delayed ? 'warning' : 'error');
    }
  })().finally(() => { if (polling === pending) polling = null; });
  pending.generation = generation;
  polling = pending;
  return pending;
}
document
  .querySelectorAll('[data-view]')
  .forEach((e) =>
    e.addEventListener('click', () => switchView(e.dataset.view)),
  );
;
;
;
;
$('startup-retry').onclick = () => poll();
$('panel-status-retry').onclick = async () => {
  const button = $('panel-status-retry');
  button.disabled = true;
  try { await poll(); } finally { button.disabled = false; }
};
window.addEventListener('popstate', () => restoreNavigation());
;
;
$('load').addEventListener('click', () => {try{return act('/api/load',storageControls.selection([...selection]));}catch(error){notice(error.message);}});
$('about').addEventListener('click', () => $('about-dialog').showModal());
document
  .querySelectorAll('[data-close]')
  .forEach((e) =>
    e.addEventListener('click', () => $(e.dataset.close).close()),
  );
document.querySelectorAll('dialog').forEach((e) =>
  e.addEventListener('click', (event) => {
    if (event.target === e) {
      const r = e.getBoundingClientRect();
      if (
        event.clientX < r.left ||
        event.clientX > r.right ||
        event.clientY < r.top ||
        event.clientY > r.bottom
      )
        e.close();
    }
  }),
);
$('dismiss').addEventListener('click', () => {
  const identity = notificationKey;
  dismissed = identity;
  $('toast').hidden = true;
  if (identity && identity === backendNotice) api('/api/dismiss', {}).catch(() => {});
});
document.querySelector('.brand').addEventListener('click', (e) => {
  e.preventDefault();
  switchView('setup');
});
;
poll().finally(() => setInterval(poll, 2000));
// No unload/visibility handler: closing or hiding Chrome never ends the session.

;
;
;
;
;
;
;
;
$('log').addEventListener('scroll', () => {
  if (!logWriting && $('log').clientHeight)
    logFollow =
      $('log').scrollHeight - $('log').scrollTop - $('log').clientHeight < 40;
});
$('log-details').addEventListener('change', renderActivity);
for (const [id,full] of [['events',false],['output',true]]) $('iz2sciv-log-'+id).addEventListener('click',()=>{
  iz2scivLogFull=full;logFollow=true;renderActivity();
});
$('download-session-log').addEventListener('click', () => {
  const id = (state.active || state.lastAttempt)?.id;
  if (id) downloadSessionLog(id);
});
$('download-saved-log').addEventListener('click', () => {
  if (!savedLogAttempt) return;
  const id = savedLogAttempt, generation = savedLogGeneration;
  async function download() {
    const button = $('download-saved-log');
    button.disabled = true;
    $('saved-log-error').hidden = true;
    await downloadSessionLog(id, error => {
      if (generation !== savedLogGeneration) return;
      showSavedLogError(error, 'Could not download the log.', 'Try the download again. You can still read the log here.', download);
      $('saved-log-retry').focus();
    });
    if (generation === savedLogGeneration) button.disabled = false;
  }
  void download();
});
document.querySelector('.activity').addEventListener('toggle', (event) => {
  if (event.target.open && logFollow)
    $('log').scrollTop = $('log').scrollHeight;
});

;

;
;
;

;

;
;

;
;
;
;
;

;

window.addEventListener('run21:languagechange', () => {
  updateNavigation();
  if (!state) return;
  renderConnectionStatus();
  iz2scivUI.render(state);
  historyStamp = '';
  $('log').dataset.stamp = '';
  renderHistory();
  renderActivity();
});

for (const placeholder of document.querySelectorAll('[data-folder]')) {
  const button = folderButton(placeholder.dataset.folder, null, globalThis.RUN21_LANGUAGE?.source(placeholder.textContent.trim())||placeholder.textContent.trim());
  if (placeholder.id) button.id = placeholder.id;
  button.dataset.folder = placeholder.dataset.folder;
  button.className += ' ' + placeholder.className;
  placeholder.replaceWith(button);
}

;
;
;
;
;
;

;
;
;

;
;
;
;
;

} // Ordinary panel; the standalone files preview above never starts polling.
