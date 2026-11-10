'use strict';
(function(root){
 function session(state){
  const civ=state?.civLauncher,active=state?.active;
  const running=active?.status==='running'||civ?.state==='running';
  const retained=!!active||['running','offline','starting','stopping'].includes(civ?.state);
  const offline=civ?.state==='offline'||active?.status==='offline'||!!state?.offline||retained&&!!state?.link&&['waiting-for-dock','waiting-for-network','offline'].includes(state.link.state);
  const stopping=active?.status==='stopping'||civ?.state==='stopping';
  const unconfirmed=stopping&&(active?.nativeParentRetention?.awaitingActualClose||civ?.shutdownUnconfirmed);
  return {running,offline,stopping,unconfirmed:!!unconfirmed,compact:running||offline||stopping&&active?.kind!=='bootstrap',title:unconfirmed?'Shutdown unconfirmed':stopping?'Disconnecting\u2026':offline?'Connection interrupted':running?'Session is running':'Connection',game:['civswitch','civswitch-ldn'].includes(state?.platform?.loaderId)?'Civilization VII':'SmileBASIC 4',launcher:(civ?.shell||state?.platform?.variant)==='izpslauncher'?'IZPSlauncher':(civ?.shell||state?.platform?.variant)==='original'?'Core only':'IZlauncher'};
 }
 function directNetwork(state){
  if(!state?.platform||['civswitch','civswitch-ldn'].includes(state.platform.loaderId)||state.active?.kind==='bootstrap')return false;
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
  return {complete,removed,label:complete?(removed?(job.target==='sd'?'Removed from SD':'Removed from session'):job.target==='sd'?'Installed on SD':'Ready in menu'):job.status,failed:job.status==='failed',active:['queued','loading','publishing'].includes(job.status)};
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
 function versions(state,pkg){
  const has=row=>(typeof row==='string'?row:row?.id)===pkg.id,session=!state.platform?.sdLauncher&&(state.installed||[]).find(has),sd=[...(state.sdInstalled||[]),...(state.platform?.sdLauncher?state.installed||[]:[])].find(has);
  return {pc:pkg.version,session:session?.version||null,sd:sd?.versionVerified?sd.version:null,onSd:!!sd,onSession:!!session,sdUnknown:!!sd&&!sd.versionVerified,sdUpdate:!!sd?.versionVerified&&sd.sourceSha256!==pkg.sha256};
 }
 function library(state){
  const packages=[...(state.library||[])],known=new Set(packages.map(p=>p.id));
  if(['civswitch','civswitch-ldn'].includes(state.platform?.loaderId))return packages;
  for(const entry of [...(state.sdInstalled||[]),...(state.platform?.sdLauncher?state.installed||[]:[])]){
   const row=typeof entry==='string'?{id:entry}:entry;if(!row?.id||known.has(row.id))continue;known.add(row.id);
   packages.push({...row,name:row.name||row.id,version:row.versionVerified?row.version:null,bytes:row.bytes||0,icon:row.id==='iz-reconnect'?'/reconnect.svg':row.icon||'/icon.svg',description:'Installed on SD. The PC package is not available.',supported:false,sdOnly:true,sd:{compatible:true}});
  }
  return packages;
 }
 // Console inventory only: never infer RAM presence from PC files or startup choices.
 function consoleApps(state){
  if(!state?.connected)return [];
  const civ=['civswitch','civswitch-ldn'].includes(state.platform?.loaderId),sdMode=!!state.platform?.sdLauncher;
  const ram=new Set((sdMode?[]:state.installed||[]).map(row=>typeof row==='string'?row:row.id));
  const sd=new Set((civ?[]:[...(state.sdInstalled||[]),...(sdMode?state.installed||[]:[])]).map(row=>typeof row==='string'?row:row.id));
  const packages=library(state),known=new Set(packages.map(p=>p.id));
  for(const entry of state.installed||[]){const row=typeof entry==='string'?{id:entry}:entry;if(row?.id&&!known.has(row.id)){packages.push({id:row.id,name:row.name||row.id,icon:row.id==='iz-reconnect'?'/reconnect.svg':row.icon||'/icon.svg'});known.add(row.id);}}
  return packages.filter(p=>ram.has(p.id)||sd.has(p.id)).map(p=>({id:p.id,name:p.name,icon:p.icon||'/icon.svg',ram:ram.has(p.id),sd:sd.has(p.id)}));
 }
 // Status is evaluated for the selected destination, so “Not installed + SD”
 // also finds apps which already have a separate RAM copy.
 function libraryFilter(state,{query='',status='all',location='all',focusId=null}={}){
  state||={};const q=query.trim().toLocaleLowerCase(),rows=library(state).filter(p=>(!focusId||p.id===focusId)&&(!q||[p.name,p.description,p.id].join(' ').toLocaleLowerCase().includes(q)));
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
  if(/package|application|module|launcher stopped|\.izm|\u043f\u0440\u0438\u043b\u043e\u0436|\u043c\u043e\u0434\u0443\u043b/.test(text))return 'application';
  return 'other';
 }
 function attempt(row,{activeId}={}){
  const success=['complete','running'].includes(row.status)||!!row.connectedAt&&['running','offline','stopped'].includes(row.status);
  const outcome=success?'successful':terminal.has(row.status)?'unsuccessful':'pending';
  const label={failed:'Failed',cancelled:'Cancelled',complete:'Completed',stopped:success?'Completed':'Stopped',running:'Running',offline:'Connection interrupted',starting:'Starting',typing:'Entering commands',stopping:'Stopping'}[row.status]||'Interrupted';
  return {success,outcome,label,errorType:outcome==='unsuccessful'?errorType(row):null,tone:outcome==='unsuccessful'?'error':success?'success':'neutral',deletable:terminal.has(row.status)&&row.id!==activeId};
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
 const api={session,connectionMode,directNetwork,deviceLinks,job,transfer,versions,library,consoleApps,libraryFilter,attempt,history,statistics,errorLabels};root.IZ2S_PRESENTATION=api;if(typeof module==='object'&&module.exports)module.exports=api;
})(globalThis);
