'use strict';
// BEGIN STARTUP
'use strict';
// Saved choices are suggestions for this game/menu only; enabling loading is
// deliberately not persisted. Legacy global choices never become selections.
(function(root){
 function scope(state){
  const civ=['civswitch','civswitch-ldn'].includes(state?.platform?.loaderId);
  return (civ?'civ7':'sb4')+':'+(civ?state?.civLauncher?.shell||'izlauncher':state?.platform?.variant||'original');
 }
 function create(preferences){
  const selected=new Set();let current=null;
  function activate(state){
   const next=scope(state);if(next===current)return false;
   current=next;selected.clear();
   try{const ids=JSON.parse(preferences?.getItem('iz2s.startupChoices.v2.'+current)||'[]');if(Array.isArray(ids))for(const id of ids)if(typeof id==='string'&&/^[a-z0-9][a-z0-9-]{0,47}$/.test(id))selected.add(id);}catch{}
   return true;
  }
  function save(){if(!current)return;try{preferences?.setItem('iz2s.startupChoices.v2.'+current,JSON.stringify([...selected]));}catch{}}
  function payload(enabled){return {moduleIds:enabled?[...selected]:[],restoreInstalled:false};}
  return {selected,activate,save,payload};
 }
 const api={scope,create};root.IZ2S_STARTUP=api;if(typeof module==='object'&&module.exports)module.exports=api;
})(globalThis);
// END STARTUP
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
  else if(/package changed|selected package|version.*(?:differ|mismatch)/.test(s)){title='The selected package has changed.';action='Refresh App library and select the current version.';}
  else if(/failed to fetch|socket hang up|disconnected|connection.*(?:lost|refused|ended)|server.*unavailable/.test(s)){title='Connection was interrupted.';action='Check the network and restore the connection when the console is ready.';}
  else if(/timeout|timed out|aborted due/.test(s)){title='The operation timed out.';action='Check the device and connection before trying again.';}
  else if(/cancelled|canceled/.test(s)){title='The operation was cancelled.';action='Review the current console screen before continuing.';}
  else {title=detail.split('\n').find(l=>l.trim())||'The operation failed.';if(title.length>220)title='The operation failed.';action='Open technical details or download the full log.';}
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
// END PRESENTATION
// BEGIN ARM INSTALLER
// Convert the existing archive download when a running older panel server
// cannot yet serve the standalone installer. No server restart is required.
(function(root){
 const template="#!/usr/bin/env python3\n\"\"\"IZ2S ARM installer: USB keyboard + controller for Civ7 and SmileBASIC 4.\"\"\"\nimport argparse\nimport base64\nimport hashlib\nimport importlib.util\nimport io\nimport json\nimport os\nfrom pathlib import Path\nimport sys\nimport tarfile\nimport tempfile\n\nARCHIVE_SHA256 = '__IZ2S_SHA256__'\nARCHIVE_BASE64 = '__IZ2S_PAYLOAD__'\n\n\ndef payload():\n    archive = base64.b64decode(ARCHIVE_BASE64, validate=True)\n    if hashlib.sha256(archive).hexdigest() != ARCHIVE_SHA256:\n        raise ValueError('Installer archive checksum mismatch. Download it again.')\n    files = {}\n    with tarfile.open(fileobj=io.BytesIO(archive), mode='r:gz') as bundle:\n        for member in bundle.getmembers():\n            parts = member.name.split('/')\n            if len(parts) != 2 or parts[0] != 'arm-input' or not member.isfile() or not member.size <= 2 * 1024 * 1024:\n                raise ValueError('Invalid installer entry')\n            name = parts[1]\n            if not name or name in ('.', '..') or '\\\\' in name or name in files:\n                raise ValueError('Invalid installer entry name')\n            files[name] = bundle.extractfile(member).read()\n    sums = json.loads(files['checksums.json'])\n    expected = sums['files']\n    if sums['algorithm'] != 'sha256' or set(files) != set(expected) | {'checksums.json'}:\n        raise ValueError('Installer file inventory mismatch')\n    for name, digest in expected.items():\n        if hashlib.sha256(files[name]).hexdigest() != digest:\n            raise ValueError('Installer checksum mismatch: ' + name)\n    package = json.loads(files['package.json'])\n    if package['id'] != 'arm-input' or not {'usb-keyboard', 'usb-gamepad', 'automatic-chat'} <= set(package['capabilities']):\n        raise ValueError('This installer requires the keyboard + controller package')\n    return files, package\n\n\ndef check_host():\n    if not sys.platform.startswith('linux'):\n        raise RuntimeError('Run this installer on the ARM Linux input device.')\n    if not Path('/run/systemd/system').is_dir():\n        raise RuntimeError('A running systemd installation is required on ARM.')\n    controllers = list(Path('/sys/class/udc').glob('*'))\n    if len(controllers) != 1:\n        raise RuntimeError('One USB device/OTG controller is required. Enable the board USB device driver first.')\n    return controllers[0].name\n\n\ndef main():\n    parser = argparse.ArgumentParser(description=__doc__)\n    mode = parser.add_mutually_exclusive_group()\n    mode.add_argument('--check-package', action='store_true', help='Verify the embedded package without changing this computer')\n    mode.add_argument('--check', action='store_true', help='Verify the package and ARM prerequisites without installing')\n    parser.add_argument('--folder', default='/opt/iz2s/arm-input')\n    parser.add_argument('--hid', default='/dev/hidg0')\n    args = parser.parse_args()\n    files, package = payload()\n    if args.check_package:\n        print(json.dumps({'verified': True, 'version': package['version'], 'capabilities': package['capabilities']}))\n        return\n    controller = check_host()\n    if args.check:\n        print(json.dumps({'verified': True, 'usbController': controller, 'version': package['version']}))\n        return\n    if os.geteuid() != 0:\n        raise RuntimeError('Use sudo python3 ' + Path(sys.argv[0]).name + ' on the ARM device.')\n    with tempfile.TemporaryDirectory(prefix='iz2s-arm-install-') as temporary:\n        folder = Path(temporary)\n        for name, data in files.items():\n            (folder / name).write_bytes(data)\n        spec = importlib.util.spec_from_file_location('iz2s_arm_install', folder / 'install.py')\n        installer = importlib.util.module_from_spec(spec)\n        spec.loader.exec_module(installer)\n        encoded = {name: base64.b64encode(data).decode() for name, data in files.items()}\n        result = installer.install(args.folder, encoded, args.hid, dual_hid=True)\n    print(json.dumps(result))\n\n\nif __name__ == '__main__':\n    try:\n        main()\n    except (Exception, KeyboardInterrupt) as error:\n        print('Installation stopped: ' + (str(error) or 'cancelled'), file=sys.stderr)\n        sys.exit(1)\n";
 async function create(archive){
  const bytes=new Uint8Array(archive);
  if(bytes.length<32||bytes.length>4*1024*1024||bytes[0]!==31||bytes[1]!==139||bytes[2]!==8)throw Error('Invalid ARM input archive');
  const digest=Array.from(new Uint8Array(await root.crypto.subtle.digest('SHA-256',bytes)),v=>v.toString(16).padStart(2,'0')).join('');
  const chunks=[];for(let at=0;at<bytes.length;at+=8192)chunks.push(String.fromCharCode(...bytes.subarray(at,at+8192)));
  return template.replace('__IZ2S_SHA256__',digest).replace('__IZ2S_PAYLOAD__',root.btoa(chunks.join('')));
 }
 root.IZ2S_ARM_INSTALLER={create};
})(globalThis);
// END ARM INSTALLER
// BEGIN STORAGE CONTROLS
'use strict';
// Shared destination policy and controls for the library and module tabs.
// Rendering reads the existing snapshot only; it never polls the console.
(function(root){
 const civ=state=>['civswitch','civswitch-ldn'].includes(state?.platform?.loaderId);
 const targets=['session','sd'],label={session:'Session memory',sd:'SD card'},idOf=row=>typeof row==='string'?row:row?.id;
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
   const manage=el('details','storage-more'),manageBody=el('div','storage-more-body');manage.append(el('summary','','Manage installation'),manageBody);
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
 const api={installed,compatible,destination,reason,filesystemWarning,payload,create};root.IZ2S_STORAGE=api;if(typeof module==='object'&&module.exports)module.exports=api;
})(globalThis);
// END STORAGE CONTROLS
// BEGIN WORKSPACE TABS
globalThis.IZ2S_WORKSPACE = {create({document,api,notice,onSelect,invoke,storageControls}) {
  const tabs=document.getElementById('workspace-tabs'),home=document.getElementById('workspace-home'),panes=document.getElementById('workspace-tools'),dock=document.getElementById('workspace-dock');
  const rows=new Map(),records=new Map(),seen=new Map(),modules=new Map(),moduleViews=new Set(),closed=new Map();
  let active='home',snapshot,available=true;
  const el=(tag,text,className)=>{const node=document.createElement(tag);if(text)node.textContent=text;if(className)node.className=className;return node;};
  const action=(label,fn,className)=>{const button=el('button',label,className);button.type='button';button.onclick=()=>Promise.resolve().then(fn).catch(error=>notice(error.message));return button;};
  const icon=name=>{const svg=document.createElementNS('http://www.w3.org/2000/svg','svg'),use=document.createElementNS(svg.namespaceURI,'use');svg.setAttribute('aria-hidden','true');use.setAttribute('href','#'+name);svg.append(use);return svg;};
  const label=(node,text)=>{if(node.dataset.sourceText!==text){node.dataset.sourceText=text;node.textContent=text;}};
  const statusText=record=>record.pending?'Stopping…':record.opening?'Opening…':record.state==='running'?'Running':record.state==='retained'?'Restart console required':record.state==='stopped'?'Stopped':'Unavailable';
  const moduleTitle=id=>{const module=modules.get(id);return module?.name+([...modules.values()].filter(item=>item.name===module?.name).length>1?' · '+(module.version||id):'');};
  try{for(const [id,value] of JSON.parse(sessionStorage.getItem('iz2s.closedTools')||'[]'))closed.set(id,value);}catch{}
  const remember=()=>{try{sessionStorage.setItem('iz2s.closedTools',JSON.stringify([...closed]));}catch{}};
  const mainWrap=el('div',null,'workspace-tab'),mainTab=action('Overview',()=>select('home'),'workspace-tab-label');
  mainWrap.dataset.tabId='home';mainTab.setAttribute('role','tab');mainTab.id='workspace-tab-home';mainTab.setAttribute('aria-controls','workspace-home');mainWrap.append(mainTab);tabs.append(mainWrap);
  function selection() {
    mainWrap.classList.toggle('active',active==='home');mainTab.setAttribute('aria-selected',String(active==='home'));mainTab.tabIndex=active==='home'?0:-1;
    for(const [id,row]of rows){const selected=id===active;row.pane.hidden=!selected;row.tab.classList.toggle('active',selected);row.button.setAttribute('aria-selected',String(selected));row.button.tabIndex=selected?0:-1;}
    for(const row of dock.children){const record=records.get(row.dataset.workspaceId),selected=row.dataset.workspaceId===active||(active.startsWith('module-info-')&&record?.kind==='module'&&record.moduleId===active.slice(12));row.classList.toggle('active',selected);const button=row.querySelector('button');if(selected)button.setAttribute('aria-current','page');else button.removeAttribute('aria-current');}
    tabs.hidden=rows.size===0;document.body.classList.toggle('workspace-active',active!=='home');
  }
  function select(id) {
    if(id!=='home'&&!rows.has(id))return;
    active=id;home.hidden=id!=='home';panes.hidden=id==='home';selection();
    onSelect?.(id==='home'?{id:'home',title:'Overview'}:{id,title:rows.get(id).title});
  }
  function close(id) {
    const row=rows.get(id);if(!row)return;
    const order=['home',...rows.keys()],previous=order[Math.max(0,order.indexOf(id)-1)];
    const record=records.get(id);if(record)closed.set(id,record.requestId);
    remember();row.pane.remove();row.tab.remove();rows.delete(id);if(active===id)select('home');else selection();renderDock();
  }
  function make(id,title) {
    if(rows.has(id)){const row=rows.get(id);row.title=title;label(row.button,title);row.button.title=title;row.dismiss.setAttribute('aria-label','Close view: '+title);return row;}
    const tab=el('div',null,'workspace-tab'),button=action(title,()=>select(id),'workspace-tab-label'),dismiss=action('×',()=>close(id),'workspace-close'),pane=el('section',null,'workspace-pane');
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
    label(row.status,statusText(record));row.status.dataset.state=record.state;
    label(row.reload,ready?'Reload view':'Start / open');row.reload.disabled=!!record.pending||!!record.opening||retained||(!available&&!ready)||(!ready&&!!snapshot?.preview);
    label(row.stop,record.pending?'Stopping…':record.kind==='settings'?'Stop settings':'Stop module');row.stop.disabled=!!record.pending||!!record.opening||retained||record.state==='stopped'||(!record.moduleId&&record.kind!=='files')||!available||!!snapshot?.preview;
    const signature=record.url+'|'+record.state;
    if(row.signature===signature)return row;
    row.signature=signature;row.content.replaceChildren();row.frame=null;
    if(!ready)row.content.append(el('p',retained?'This module could not confirm native cleanup. Restart the console before starting it again.':'The interface is stopped. Use Start / open to return.','workspace-empty'));
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
    const previousClose=closed.get(id);
    record.opening=true;renderDock();if(rows.has(id))renderRecord(record);
    try{const result=await api('/api/workspace/open',{id});if(result.tab&&records.get(id)?.requestId===record.requestId){const dismissed=closed.get(id)!==previousClose;if(dismissed){closed.set(id,result.tab.requestId);remember();}openRequest(result.tab,!dismissed);}}
    finally{const current=records.get(id);if(current){current.opening=false;if(rows.has(id))renderRecord(current);}renderDock();}
  }
  async function stopRecord(id) {
    const record=records.get(id);if(!record||record.pending||record.opening||!available)return;
    record.pending=true;renderDock();if(rows.has(id))renderRecord(record);
    try{const result=await api('/api/workspace/stop',{id});if(!records.has(id)||records.get(id).requestId!==record.requestId)return;const current=result.tab||records.get(id);if(!result.tab)current.state='stopped';records.set(id,{...current,title:record.title,pending:true});if(rows.has(id))renderRecord(records.get(id));}
    finally{const current=records.get(id);if(current){current.pending=false;if(rows.has(id))renderRecord(current);}renderDock();}
  }
  function openRequest(record,force=false) {
    if(!record||!/^[-a-z0-9]{1,80}$/.test(record.id))throw Error('Invalid module tab');
    const url=new URL(record.url);if(url.protocol!=='http:'||url.hostname!=='127.0.0.1'||url.username||url.password)throw Error('Module interface must be local');
    const previous=records.get(record.id);record={...record,...(record.kind==='files'?{title:'Super File Bros.'}:{}),pending:previous?.pending,opening:previous?.opening};
    records.set(record.id,record);const fresh=seen.get(record.id)!==record.requestId;seen.set(record.id,record.requestId);
    if(force||fresh&&!record.opening&&closed.get(record.id)!==record.requestId&&record.state!=='stopped'){closed.delete(record.id);remember();renderRecord(record);if(force||record.kind!=='files'&&!record.automatic)select(record.id);}
    else if(rows.has(record.id))renderRecord(record);
    renderDock();
  }
  function dockRow(id,title,status,callback,kind,state='stopped') {
    const row=el('div',null,'workspace-dock-row'),open=action('',callback,'workspace-dock-open'),text=el('span',null,'workspace-dock-text');
    row.dataset.workspaceId=id;row.dataset.state=state;open.title=title;text.append(el('span',title,'workspace-dock-name'));open.append(icon(kind==='files'?'i-folder':'i-grid'),text);row.append(open,el('small',status,'workspace-dock-state'));return {row,open};
  }
  function renderDock() {
    const stamp=JSON.stringify([available,!!snapshot?.preview,[...records.values()].map(r=>[r.id,r.title,r.url,r.requestId,r.state,r.pending,r.opening,r.moduleId]),[...moduleViews].map(id=>[id,moduleTitle(id),snapshot?.installed,snapshot?.moduleStates])]);
    if(dock.dataset.stamp===stamp){selection();return;}dock.dataset.stamp=stamp;dock.replaceChildren();
    for(const record of records.values()){
      const {row,open}=dockRow(record.id,record.title,statusText(record),()=>['running','retained'].includes(record.state)?openRequest(record,true):startRecord(record.id),record.kind,record.pending?'pending':record.opening?'opening':record.state),stop=action('',()=>stopRecord(record.id),'workspace-dock-stop');
      stop.append(icon('i-stop'));
      stop.setAttribute('aria-label','Stop '+record.title);stop.title='Stop '+record.title;stop.disabled=!!record.pending||!!record.opening||['stopped','retained'].includes(record.state)||(!record.moduleId&&record.kind!=='files')||!available||!!snapshot?.preview;open.disabled=!!record.pending||!!record.opening;row.append(stop);dock.append(row);
    }
    for(const id of moduleViews){
      const module=modules.get(id);if(!module||[...records.values()].some(record=>record.moduleId===id&&record.kind==='module'))continue;
      const loaded=snapshot?.installed?.some(item=>(typeof item==='string'?item:item.id)===id),running=snapshot?.moduleStates?.some(item=>item.id===id&&item.state==='running');
      dock.append(dockRow('module-info-'+id,moduleTitle(id),running?'Running':loaded?'Loaded on console':'Not running',()=>openModule(id),'module',running?'running':loaded?'loaded':'stopped').row);
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
    const pkg=snapshot?.library?.find(item=>item.id===id),content=el('div',null,'workspace-module-copy'),heading=el('h1',module.name);
    heading.setAttribute('translate','no');content.append(heading,el('p',module.version||pkg?.version||'','hint'),el('p',pkg?.description||module.description||'','subtitle'));
    if(pkg?.session?.compatible===false)content.append(el('p',pkg.session.reason||'Unavailable for this game','hint'));
    else if(pkg?.session?.requiresFilesystem)content.append(el('p','Requires filesystem access','hint'));
    content.append(action('App library',()=>{select('home');document.querySelector('[data-view="apps"]')?.click();},'secondary'));
    if(module.switch)content.append(el('p','This module runs on the console. Use this tab to load or remove it.','hint'));
    row.pane.append(toolbar,content);
  }
  function openModule(id) {
    if(!modules.has(id))throw Error('Module is no longer installed');
    const record=[...records.values()].find(record=>record.moduleId===id&&record.kind==='module');if(record){openRequest(record,true);return rows.get(record.id);}
    const row=make('module-info-'+id,moduleTitle(id));moduleViews.add(id);moduleContent(id);select('module-info-'+id);renderDock();return row;
  }
  function updateModules(state) {
    const stamp=JSON.stringify([state.moduleCatalog,state.moduleStates,state.installed,state.sdInstalled,state.sdStorage,state.jobs?.map(j=>[j.packageId,j.target,j.status]),state.library?.map(p=>[p.id,p.name,p.version,p.description,p.module,p.session,p.sd]),state.connected,state.preview,state.platform?.residentGraphics,state.platform?.sdLauncher]);
    const ended=snapshot?.connected&&(!state.connected||snapshot.active?.id!==state.active?.id);
    snapshot=state;if(updateModules.stamp===stamp&&!ended)return;updateModules.stamp=stamp;
    if(ended)for(const id of [...moduleViews]){moduleViews.delete(id);close('module-info-'+id);}
    modules.clear();for(const module of state.moduleCatalog||[])modules.set(module.id,module);
    // Imported IZM packages also have a details page, even without a source module.
    for(const pkg of state.library||[]){const id=pkg.module?.id||pkg.id;if(!modules.has(id))modules.set(id,{...pkg.module,id,name:pkg.name,version:pkg.version,switch:pkg.module?.switch||{}});}
    for(const id of moduleViews){if(!modules.has(id)){close('module-info-'+id);moduleViews.delete(id);}else if(rows.has('module-info-'+id)){make('module-info-'+id,moduleTitle(id));moduleContent(id);}}
    for(const [id,record]of records)if(rows.has(id))renderRecord(record);renderDock();
  }
  function setAvailable(value){if(available===value)return;available=value;for(const [id,record]of records)if(rows.has(id))renderRecord(record);for(const id of moduleViews)if(rows.has('module-info-'+id))moduleContent(id);renderDock();}
  function sync(entries){
    const present=new Set(entries.map(entry=>entry.id));
    for(const id of [...records.keys()])if(!present.has(id)){
      records.delete(id);seen.delete(id);closed.delete(id);close(id);
    }
    remember();for(const entry of entries)openRequest(entry);renderDock();
  }
  tabs.addEventListener('keydown',event=>{
    if(!['ArrowLeft','ArrowRight','Home','End'].includes(event.key))return;
    const buttons=[...tabs.querySelectorAll('[role=tab]')],index=buttons.indexOf(document.activeElement);if(index<0)return;event.preventDefault();
    const next=event.key==='Home'?0:event.key==='End'?buttons.length-1:(index+(event.key==='ArrowRight'?1:-1)+buttons.length)%buttons.length;buttons[next].click();buttons[next].focus();
  });
  window.addEventListener('run21:languagechange',()=>{for(const row of rows.values())if(row.frame)shareLanguage(row.frame);});
  window.addEventListener('message',event=>{if(event.data?.type!=='iz2s:workspace-ready')return;for(const row of rows.values())if(row.frame&&event.source===row.frame.contentWindow&&event.origin===new URL(row.frame.src).origin)shareLanguage(row.frame);});
  select('home');return{select,openRequest,openModule,updateModules,sync,close,setAvailable};
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
  const hiddenEngines = new Set(['pepsipu', 'izmysh-resume']);
  const izEngines = new Set(['izmysh', 'bermthesequel']);
  const izVariants = new Set(['izlauncher', 'izpslauncher']);

  function visible(descriptor) {
    return !hidden.has(descriptor.id) && !/^ps2-(?:cold-r|cpu-accounting-r|sd-admit-r|sd-engine-r)/.test(descriptor.id) && !['test','diagnostic'].includes(descriptor.store?.channel);
  }

  function engineAvailable(loader, variant) {
    if (!loader || hiddenEngines.has(loader.id) || loader.supported === false) return false;
    const declared = Array.isArray(loader.availableIn) ? loader.availableIn
      : Array.isArray(loader.modes) ? loader.modes : null;
    if (variant === 'original') return !declared || declared.includes('original');
    if (!izVariants.has(variant) || !izEngines.has(loader.id)) return false;
    // Older APIs declared only the common IZLauncher transport mode.
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
          availableIn: ['original', 'izlauncher', 'izpslauncher'].filter(variant => engineAvailable(loader, variant)),
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
let hardStopping = false;
let stopTask = null, pendingStart = false, actionGeneration = 0;
let cardputerRequest=null;
let state = null,
  serverUnavailable = false,
  step = 'launch',
  busy = false,
  timer = null,
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
const startupChoices=globalThis.IZ2S_STARTUP.create(startupPreferences);
const startupSelection=startupChoices.selected;
let startupStamp = '';
// The manual-input design is a local UI preview, separate from session settings.
let inputDesign = 'arm', manualDesignStage = 1;
const advSetup = globalThis.IZ2S_ADV_SETUP.create({document, changed:()=>{if(state)renderControls();}, confirmUsb:()=>act('/api/cardputer/confirm-usb',{})});
const civStartup=()=>startupChoices.payload($('preload-enabled').checked);
const civswitchLdnUI = globalThis.IZ2S_CIVSWITCH_LDN.create({document,act,getStartup:civStartup,isUnavailable:()=>serverUnavailable});
const civswitchUI = globalThis.IZ2S_CIVSWITCH.create({document, act,getStartup:civStartup,isUnavailable:()=>serverUnavailable,downloadArm:()=>downloadArmInstaller('civ7'),setupInput:()=>openDeviceSettings(state?.platform?.configuration?.inputAdapter==='cardputer-wifi'?{tab:1}:{})});
let workspaceReady = false;
let installPreferences;try{installPreferences=localStorage;}catch{}
const storageControls=globalThis.IZ2S_STORAGE.create({document,getState:()=>state,getBusy:()=>busy||serverUnavailable,invoke:act,changed:()=>renderControls(),notice,confirm:(message,name)=>confirmRemoval(message,name),preferences:installPreferences});
const workspace = globalThis.IZ2S_WORKSPACE.create({document,api,notice,invoke:act,storageControls,onSelect:tab=>{
  const selected=document.querySelector('.view:not([hidden])')?.id||'setup';
  const title=tab.id==='home'?({setup:'Connection',apps:'App library',history:'Attempts'}[selected]||'IZ2S'):tab.title;
  document.querySelectorAll('.sidebar [data-view]').forEach(button=>{const current=tab.id==='home'&&button.dataset.view===selected;button.classList.toggle('active',current);if(current)button.setAttribute('aria-current','page');else button.removeAttribute('aria-current');});
  $('view-title').textContent=title;document.title=title+' · IZ2S Platform';
}});
const manualDesignConfirmed = new Set();
function syncStartupChoices(){
 if(state&&startupChoices.activate(state)){
  $('preload-enabled').checked=false;
  startupStamp='';
 }
}
function saveStartup(){startupChoices.save();}
$('preload-enabled').checked=false;
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
    button.dataset.pending = 'true';
    button.disabled = true;
    try { await api('/api/folder/open', { kind, ...(id ? { id } : {}) }); }
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
        ...(data === undefined ? {} : { 'content-type': 'application/json' }),
      },
      ...(data === undefined ? {} : { body: JSON.stringify(data) }),
      ...(controller ? { signal: controller.signal } : {}),
    });
    const result = await res.json();
    if (!res.ok) throw Object.assign(Error(result.error || 'The server did not respond'), {statusCode: res.status});
    return result;
  } finally {
    if (deadline !== null) clearTimeout(deadline);
  }
}
function notice(message, type = 'error', identity = message) {
  if (identity === dismissed) return;
  notificationKey = identity;
  $('toast').hidden = false;
  $('toast').classList.toggle('success', type === 'success');
  $('toast-title').textContent =
    type === 'success' ? 'All set.' : 'Something needs attention.';
  const problem=globalThis.IZ2S_ERRORS.describe(message);
  $('toast-text').textContent = type==='success'?message:problem.title;
  $('toast-action').hidden=type==='success';$('toast-action').textContent=problem.action;
  $('toast-details').hidden=type==='success';$('toast-diagnostic').textContent=problem.detail;$('toast-text').setAttribute('translate',type==='error'?'no':'yes');
  $('toast').querySelector('.toast-symbol').textContent =
    type === 'success' ? '✓' : '!';
}
async function act(route, data = {}) {
  if (route === '/api/stop') return stopSession();
  if (inputDesign === 'own' && !['/api/stop', '/api/hard-stop', '/api/dismiss'].includes(route)) {
    notice('This is a manual input preview. Changes are not sent to the device.');
    return;
  }
  if (busy) return;
  const generation = ++actionGeneration;
  busy = true;
  pendingStart = route === '/api/start';
  renderControls();
  try {
    const result = await api(route, data);
    if (generation === actionGeneration) await poll();
    return generation === actionGeneration ? result : undefined;
  } catch (e) {
    if (generation === actionGeneration) notice(e.message);
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
  $('screen-ready').checked = false;
  document.querySelectorAll('.up-ready,.up-source-ready').forEach(input => {input.checked = false;});
  stopTask = Promise.resolve().then(async () => {
    try {
      const result = await api('/api/stop', {});
      if(result?.retained)notice(result.error||'Shutdown unconfirmed','error');
      // Completion must not wait on catalog, workspace or an older status read.
      const previousPoll = polling;
      if (previousPoll) void previousPoll.then(() => poll());
      else void poll();
      return result;
    } catch (error) {notice(error.message);}
    finally {stopTask = null; busy = false; renderControls();}
  });
  renderControls();
  return stopTask;
}
function switchView(id,{focusApp=false}={}) {
  if (!['setup', 'apps', 'history'].includes(id)) return;
  if(id==='apps'&&!focusApp&&libraryContext)leaveLibraryContext();
  workspace.select('home');
  const previous = document.querySelector('.view:not([hidden])')?.id;
  if (inputDesign === 'own' && id !== 'setup') {
    inputDesign = 'arm';
    if (state) { renderDevices(); renderControls(); }
  }
  document.querySelectorAll('.view').forEach((e) => (e.hidden = e.id !== id));
  document
    .querySelectorAll('.sidebar [data-view]')
    .forEach((e) => {const current=e.dataset.view===id;e.classList.toggle('active',current);if(current)e.setAttribute('aria-current','page');else e.removeAttribute('aria-current');});
  const title = { setup: 'Connection', apps: 'App library', history: 'Attempts' }[id] || 'IZ2S Platform';
  $('view-title').textContent = title;
  document.title = title + ' · IZ2S Platform';
  if (previous !== id) window.scrollTo({ top: 0, left: 0, behavior: 'auto' });
}
function chooseStep(value) {
  step = value;
  requestId = null;
  $('screen-ready').checked = false;
  document
    .querySelectorAll('[data-step]')
    .forEach((e) => e.classList.toggle('selected', e.dataset.step === step));
  renderControls();
  if (state) renderWizard();
}
function fieldsDirty(container, values) {
  return Object.entries(fieldValues(container)).some(
    ([name, value]) => String(value) !== String(values?.[name] ?? ''),
  );
}
function deliveryDirty() {
  return (
    !!state?.platform?.izModules &&
    fieldsDirty($('delivery-fields'), state.platform.loaderSettings)
  );
}
let sessionModeChoice=null,sessionModeScope='';
function renderSessionConnectionMode(){
 const view=globalThis.IZ2S_PRESENTATION.connectionMode(state),scope=state?.active?.id||'idle';
 if(scope!==sessionModeScope){sessionModeScope=scope;sessionModeChoice=null;}
 const mode=sessionModeChoice||view.preferred,visible=step==='launch'&&state?.platform?.izModules===true&&!['civswitch','civswitch-ldn'].includes(state.platform.loaderId);
 $('session-mode').hidden=!visible;
 for(const value of ['new','restore']){const button=$('session-mode-'+value);button.classList.toggle('selected',mode===value);button.setAttribute('aria-pressed',String(mode===value));button.disabled=busy||serverUnavailable||value==='new'&&view.retained||value==='restore'&&!view.available;}
 $('session-mode-note').textContent=mode==='restore'||view.retained?view.note:'After a full console restart, open SmileBASIC 4 → Make a Program → Direct, then start a new launcher session. Recovery reconnects to the same running launcher.';
 $('session-mode-restore').title=view.note;
 if(!visible)return;
 const panel=$('session-mode'),summary=$('console-apps')||$('session-summary');
 if(globalThis.IZ2S_PRESENTATION.session(state).compact){if(summary?.after&&panel.previousElementSibling!==summary)summary.after(panel);}
 else if(panel.parentElement!==$('iz-flow')&&$('iz-flow')?.insertBefore)$('iz-flow').insertBefore(panel,$('delivery-options'));
 const restore=mode==='restore';
 $('restore-connection').hidden=!restore;$('restore-connection').disabled=serverUnavailable||busy||state.preview||!view.canRestore;$('restore-connection').textContent=view.label;
 if(restore){$('start').hidden=true;$('session-confirmation').hidden=true;$('launch-modules').hidden=true;$('delivery-options').hidden=true;$('gate-message').textContent=view.note;}
}
function renderControls() {
  syncStartupChoices();
  document.querySelectorAll('.segmented button,[data-step]').forEach(button=>button.setAttribute('aria-pressed',String(button.classList.contains('selected'))));
  if(state?.platform)renderArmSetup(state.platform);
  renderPlatformControls();
  const setup = state?.active?.kind === 'bootstrap' ? state.active.setup : null,
    reboot = setup?.stage === 'reboot',
    active = !!state?.active,
    ready = !!state?.checks.ready,
    ack = $('screen-ready').checked,
    initial = !active || reboot,
    retryRequired = false;
  const engineAllowed = !state?.platform || RUN21_CATALOG.engineAvailable(
    state.platform.loaders.find(loader => loader.id === state.platform.loaderId),
    state.platform.variant,
  );
  const residentLaunch=initial&&(step==='launch'||reboot)&&state?.platform?.residentGraphics===true&&['izlauncher','izpslauncher'].includes(state?.platform?.variant);
  const startupIds=state?.platform?.sdLauncher?[]:$('preload-enabled').checked?[...startupSelection]:[];
  const residentBad=residentLaunch?startupIds.map(id=>state.library.find(p=>p.id===id)).filter(p=>!residentEligibility(p)?.compatible):[];
  const residentAudioCount=startupIds.map(id=>(state?.library||[]).find(p=>p.id===id)).filter(p=>residentEligibility(p)?.code==='finite-audio-candidate').length;
  const residentIssue=residentLaunch?(residentAudioCount>1?'Select one qualified game for retained audio.':(state.library.some(p=>!residentEligibility(p))?'Restart the panel to enable module compatibility checks.':residentBad.length?'Remove incompatible apps from the retained set: '+residentBad.map(p=>p?.name||'unknown module').join(', ')+'.':!startupIds.length?'Select at least one compatible app at startup.':'')):'';
  $('recovery-option').hidden =
    step !== 'bootstrap' || !initial || !state?.recoverySetupAvailable;
  $('recovery-enabled').disabled =
    busy || !initial || !state?.recoverySetupAvailable;
  if (state && !state.recoverySetupAvailable)
    $('recovery-enabled').checked = false;
  $('launch-app-library').hidden = step !== 'launch' && !reboot;
  const civMode=['civswitch','civswitch-ldn'].includes(state?.platform?.loaderId);
  if(!civMode&&$('launch-modules').parentElement!==$('iz-flow'))$('iz-flow').insertBefore($('launch-modules'),document.querySelector('#iz-flow .launch-row'));
  $('launch-modules').hidden = civMode?$('launch-modules').hidden:state?.platform?.sdLauncher===true || (step !== 'launch' && !reboot);
  $('preload-options').hidden = !$('preload-enabled').checked;
  const preloadLocked = busy || serverUnavailable || !initial || (civMode && !!(state.platform.loaderId === 'civswitch-ldn' ? state.civswitchLdn?.busy : state.civswitch?.busy));
  $('preload-enabled').disabled = preloadLocked;
  $('preload-clear').disabled = preloadLocked;
  document
    .querySelectorAll('#preload-list input')
    .forEach((e) => (e.disabled = preloadLocked || e.dataset.unavailable === 'true' || (e.dataset.residentUnavailable === 'true'&&!e.checked)));
  $('preload-count').textContent = startupSelection.size + ' selected';
  $('start').disabled =
    busy ||
    !!state?.emergencyInputPending ||
    !initial ||
    !ready ||
    !ack ||
    !!state?.preview ||
    !state?.setupWizard ||
    !engineAllowed ||
    !!residentIssue ||
    deliveryDirty();
  $('start-label').textContent =
    reboot || step === 'launch'
      ? 'Load ' + selectedShellName()
      : 'Install ' +
        ($('recovery-enabled').checked && state?.recoverySetupAvailable
          ? state?.platform?.delivery?.recoveryBootstrapName || 'NS2R'
          : state?.platform?.delivery?.baseBootstrapName || 'NS2');
  $('start').hidden = !initial || retryRequired;
  $('retry').hidden = !retryRequired;
  $('session-confirmation').hidden = retryRequired;
  $('retry').textContent = state?.lastAttempt?.kind === 'bootstrap'
    ? 'Restart First Setup' : 'Return to Launch';
  $('gate-message').textContent = retryRequired
    ? state.lastAttempt.kind === 'bootstrap'
      ? 'Return to setup, then confirm the screen before continuing.'
      : 'Return to the form, then confirm the screen before loading.'
    : !engineAllowed
    ? 'Choose IZ2S Core or ns2jb by BermTheSequel, or use Core only.'
    : deliveryDirty()
    ? 'Saving session settings…'
    : residentIssue
      ? residentIssue
    : state?.emergencyInputPending
      ? 'Check the ARM PC connection. Press red STOP to retry input cancellation.'
      : !state?.setupWizard
        ? 'Restart Platform to activate the updated setup flow'
        : active && !reboot
          ? state.active.status==='running'?'The console session is running.':'Leave the console on the indicated screen.'
          : state?.preview
            ? 'Preview · device commands disabled'
            : !ready
              ? 'Waiting for '+(state?.checks?.items?.length || 0)+' device checks'
              : !ack
                ? 'Confirm the console screen to continue'
                : 'All checks passed · ready to start';
  $('hard-stop').hidden = !state?.emergencyInputPending && !state?.active?.stopTimedOut;
  $('hard-stop').disabled = hardStopping || !state?.hardStopAvailable;
  $('stop').hidden = !active && !pendingStart && !stopTask;
  $('stop').disabled = !!stopTask || state?.active?.status === 'stopping';
  $('stop').textContent =
    stopTask || state?.active?.status === 'stopping'
      ? state?.active?.nativeParentRetention?.awaitingActualClose && !stopTask ? 'Shutdown unconfirmed' : 'Disconnecting…'
      : state?.active?.kind === 'bootstrap'
        ? 'Cancel setup'
        : 'Stop session';
  $('screen-ready').disabled = !initial || busy;
  $('screen-ready').parentElement.hidden = !initial;
  document
    .querySelectorAll('[data-step]')
    .forEach((e) => (e.disabled = active || busy));
  $('recheck').disabled = !!state?.active?.inputActive || busy;
  const destinationError=selection.size?storageControls.selectionError([...selection]):'';
  $('load').disabled = busy || !state?.connected || !selection.size || !!state?.preview || !!destinationError;
  $('load').title=destinationError;
  storageControls.updateAll();
  document.querySelectorAll('.module-host-actions button').forEach(button => {
    const source = button.closest('.local-module')?.querySelector('.local-source');
    button.disabled = busy || !!state?.preview || button.dataset.unavailable === 'true' || (!!source && !source.value.trim());
  });
  document
    .querySelectorAll(
      '.transport-choice button,.transport-choice input,.address-form input,.address-form select,.address-form button,.detected-peer',
    )
    .forEach(
      (e) =>
        (e.disabled =
          busy ||
          !state?.connections ||
          !!state?.connections?.locked ||
          !!state?.preview ||
          e.dataset.unavailable === 'true' || e.dataset.fixed === 'true'),
    );
  const hidDisabled = busy || !ready || !!state?.preview || !state?.setupWizard;
  $('verify-source').disabled = hidDisabled || !$('review-ready').checked;
  $('skip-source').disabled =
    busy || !$('review-ready').checked || !state?.setupWizard;
  $('save-source').disabled = hidDisabled;
  $('compare-source').disabled =
    busy ||
    !$('check-lines').checked ||
    !Array.from({ length: 6 }, (_, i) => $('metric-' + i).value).every((s) =>
      /^[0-9]{1,8}$/.test(s),
    );
  $('reverify-source').disabled = hidDisabled;
  $('confirm-saved').disabled = busy || !$('saved-ready').checked;
  $('retry').disabled = busy;
  $('restart-setup').disabled = busy;
  $('edit-readings').disabled = busy;
  renderManualInputDesign();
  advSetup.render({state,step,busy,serverUnavailable,manual:inputDesign==='own',preparation:cardputerRequest||state?.hardwareOperation});
  renderSessionConnectionMode();
  renderServerAvailability();
  if(state?.platform)renderMenuLayout();
}
function renderWizard() {
  if (state?.platform && !state.platform.izModules) {
    renderControls();
    renderEngineInstall();
    return;
  }
  const row = state.active;
  if (row?.kind === 'launch') {
    step = 'launch';
    document
      .querySelectorAll('[data-step]')
      .forEach((e) =>
        e.classList.toggle('selected', e.dataset.step === 'launch'),
      );
  }
  const setup = row?.kind === 'bootstrap' ? row.setup : null,
    stage = setup?.stage || 'prepare',
    show = step === 'bootstrap' || !!setup;
  $('wizard-results').hidden = !setup?.comparison || !['mismatch', 'save'].includes(stage);
  $('wizard-steps').hidden = !show;
  renderInstallationSlot();
  const source =
      setup ||
      ($('recovery-enabled').checked && state.recoverySetupAvailable
        ? state.recoverySource
        : state.platform?.baseSource),
    lines = source?.lines || 229,
    sourceSlot = source?.sourceSlot ?? 0,
    name =
      setup?.name ||
      ($('recovery-enabled').checked && state.recoverySetupAvailable
        ? state.platform?.delivery?.recoveryBootstrapName || 'NS2R'
        : state.platform?.delivery?.baseBootstrapName || 'NS2');
  document.querySelector('[data-phase=typing]').textContent = lines + ' lines';
  document.querySelector('[data-phase=save]').textContent = 'Save ' + name;
  for (let i = 0; i < 3; i++) {
    const part = source?.parts?.[i];
    if (part)
      document.querySelector('label[for=metric-' + i * 2 + ']').textContent =
        i + 1 + ' · lines ' + part.first + '-' + part.last;
  }
  $('check-lines').nextElementSibling.textContent =
    'The check finished without errors. SOURCE LINES shows ' +
    lines +
    ', or ' +
    (lines + 1) +
    ' with only one empty final line.';
  $('save-source').firstChild.textContent = 'Save as ' + name + ' ';
  $('saved-ready').nextElementSibling.textContent =
    name + ' was saved successfully on the console.';
  $('recovery-status').hidden = !state.recoverySetupAvailable;
  $('recovery-status').textContent =
    state.link?.state === 'waiting-for-dock'
      ? 'Connection interrupted · waiting for the console to reconnect.'
      : state.link?.protocol === 2
        ? 'Automatic connection recovery is active.'
        : state.active?.kind === 'launch'
          ? 'This session uses the original NS2. Install NS2R to enable recovery.'
          : state.bootstrapProfile?.recovery
            ? (state.platform?.delivery?.bootstrapName || 'NS2R') +
              ' is selected for the next launch.'
            : 'Connection recovery needs a one-time NS2R installation.';
  const descriptions = {
    prepare:
      'In SmileBASIC, choose Make a Program and open Direct mode (the black console screen). Slot 0 must be empty. I will enter LIST 0:1 first, then the original 229 lines.',
    typing:
      'Entering the original source into EDIT0. Keep SmileBASIC open and do not switch screens.',
    review:
      'Are all 229 source lines present, with END on the last source line? An empty line 230 after the final Enter is normal. The three-part source check is optional.',
    checking:
      'Switching to Direct automatically when needed, then entering ordinary read-only source checks. The bootstrap is not executed.',
    values:
      'Read the six numbers from the Switch. The PC cannot read or validate the console output automatically.',
    mismatch:
      'Some source readings differ. Review the marked part and line range. Download a diagnostic for analysis, correct the readings if mistyped, or start again.',
    save: 'Ready to save the source as NS2. Saving is a separate action and never starts the bootstrap.',
    saving:
      'Sending the save command in Direct mode. Confirm any on-console Save / Overwrite dialog.',
    save_confirmation:
      'Has SmileBASIC saved NS2 successfully? Confirm the result on the Switch before continuing.',
    reboot:
      'Fully restart the Switch. Then open SmileBASIC → Make a Program → Direct (the black screen). Only confirm the checkbox below after this is done.',
  };
  if (show) {
    $('step-description').textContent = (
      descriptions[stage] || descriptions.prepare
    )
      .replaceAll('229', String(lines))
      .replaceAll('230', String(lines + 1))
      .replaceAll('original ', name === 'NS2R' ? 'recovery ' : 'original ')
      .replaceAll('as NS2.', 'as ' + name + '.')
      .replaceAll('saved NS2 successfully', 'saved ' + name + ' successfully')
      .replaceAll('Slot 0', 'Slot ' + sourceSlot)
      .replaceAll('LIST 0:1', 'LIST ' + sourceSlot + ':1')
      .replaceAll('EDIT0', 'EDIT' + sourceSlot);
    $('screen-label').textContent =
      stage === 'reboot'
        ? 'I fully restarted the Switch and opened fresh SmileBASIC Direct.'
        : 'I opened fresh SmileBASIC → Make a Program → Direct, slot ' +
          sourceSlot +
          ' is empty, keyboard is English (US), Caps Lock is off.';
  } else {
    $('step-description').textContent =
      row?.kind === 'launch'
          ? 'Keep SmileBASIC open. The original launch sequence is running; no memory preparation is repeated automatically.'
          : 'Use the program already saved on your console. Fully restart the Switch and open fresh SmileBASIC Direct before launching.';
    $('screen-label').textContent = state.platform?.delivery?.preloaded
      ? 'After a full restart, the bootstrap is loaded in slot 0 and Direct is open; RUN and REMOVE have not been executed.'
      : 'I fully restarted the Switch and opened fresh SmileBASIC Direct.';
  }
  if (state.platform?.configuration?.inputAdapter === 'cardputer-wifi') {
    const capsOn = state.platform.profiles?.[state.platform.configurationId]?.cardputerCapsLock === 'on';
    const brief = {
      prepare:capsOn
        ? 'Open SmileBASIC Direct. Use an empty slot, English (US), Caps Lock on.'
        : 'Open SmileBASIC Direct. Use an empty slot, English (US), Caps Lock off.',
      typing:'ADV is entering the preloaded source. Keep SmileBASIC open.',
      review:'Check the source on Switch 2, then save it.',
      checking:'ADV is entering the optional source check.',
      values:'Enter the six numbers shown on Switch 2.',
      mismatch:'The readings differ. Check the source or enter the readings again.',
      save:'Save the bootstrap on Switch 2.',
      saving:'Wait for saving to finish on Switch 2.',
      save_confirmation:'Confirm the saved file on Switch 2.',
      reboot:'Fully restart Switch 2 and open fresh SmileBASIC Direct.',
    };
    $('step-description').textContent = state.active?.status==='running'?'The launcher is running. Open App library to manage applications.':state.active?.status==='offline'?'The console session is retained. Restore the connection when the console is ready.':show ? brief[stage] || brief.prepare : 'ADV: Wi-Fi on, USB to Switch 2. Restart Switch 2 and open fresh Direct.';
    $('screen-label').textContent = show && stage !== 'reboot'
      ? 'Direct is open; slot ' + sourceSlot + ' is empty; English (US), Caps Lock ' + (capsOn ? 'on.' : 'off.')
      : 'Switch 2 was fully restarted; fresh Direct is open.';
  }
  const stamp = (row?.id || 'none') + ':' + stage;
  if (stamp !== setupStamp) {
    const previous = setupStamp.split(':').at(-1);
    setupStamp = stamp;
    if (stage === 'review') $('review-ready').checked = false;
    if (stage === 'values') {
      $('check-lines').checked = false;
      if (previous === 'checking')
        for (let i = 0; i < 6; i++) $('metric-' + i).value = '';
    }
    if (stage === 'save_confirmation') $('saved-ready').checked = false;
    if (stage === 'reboot') {
      requestId = null;
      $('screen-ready').checked = false;
    }
  }
  $('wizard-review').hidden = !setup || stage !== 'review';
  $('wizard-values').hidden = !setup || stage !== 'values';
  $('wizard-save').hidden = !setup || stage !== 'save';
  $('wizard-saved').hidden = !setup || stage !== 'save_confirmation';
  if (setup) {
    $('review-label').textContent =
      'All ' +
      lines +
      ' source lines are present, the last source line is END, and I left the console in ' +
      (setup.screen === 'editor' ? 'the editor.' : 'Direct mode.');
    $('save-note').textContent =
      setup.verification === 'passed'
        ? 'All three parts match the entered readings. The next button enters SAVE “TXT:NS2” in Direct.'
        : 'You skipped the optional check. The received source remains unverified. The next button enters SAVE “TXT:NS2” in Direct.';
    $('save-note').textContent =
      $('save-note').textContent.replaceAll('TXT:NS2', 'TXT:' + name) +
      ' · SAVE "TXT:' +
      name +
      '",' +
      sourceSlot;
    const comparison = setup.comparison;
    if (comparison && ['mismatch', 'save'].includes(stage)) {
      $('wizard-results').hidden = false;
      const fingerprint = JSON.stringify(comparison);
      if (fingerprint !== resultStamp) {
        resultStamp = fingerprint;
        $('part-results').replaceChildren();
        for (const part of comparison.parts) {
          const box = element(
            'div',
            'part-result' + (part.matches ? '' : ' mismatch'),
          );
          box.append(
            element(
              'strong',
              '',
              `Part ${part.index} · lines ${part.first}-${part.last} · ${part.matches ? 'Matches' : 'Mismatch'}`,
            ),
            element(
              'span',
              '',
              `Characters: ${part.actual.characters} / expected ${part.characters}`,
            ),
            element(
              'span',
              '',
              `Checksum: ${part.actual.checksum} / expected ${part.checksum}`,
            ),
            element(
              'span',
              '',
              `Text storage: ${part.actual.characters * 2} / expected ${part.utf16Bytes} bytes (UTF-16, derived)`,
            ),
          );
          $('part-results').append(box);
        }
      }
      $('edit-readings').hidden = stage !== 'mismatch';
      $('restart-setup').hidden = stage !== 'mismatch';
    }
  }
  const phases = ['prepare', 'typing', 'review', 'save', 'reboot', 'launch'],
    phase = ['checking', 'values', 'mismatch'].includes(stage)
      ? 'review'
      : ['saving', 'save_confirmation'].includes(stage)
        ? 'save'
        : stage;
  document.querySelectorAll('[data-phase]').forEach((e) => {
    e.classList.toggle('current', e.dataset.phase === phase);
    e.classList.toggle(
      'done',
      phases.indexOf(e.dataset.phase) < phases.indexOf(phase),
    );
  });
  renderControls();
  renderEngineInstall();
}

const networkName = (mode) => (mode === 'wifi' ? 'Wi-Fi' : 'LAN');
async function changeConnection(id, mode, ip, modes) {
  const result = await act('/api/connection', {
    id,
    ...(mode === undefined ? {} : { mode }),
    ...(ip === undefined ? {} : { ip }),
    ...(modes === undefined ? {} : { modes }),
  });
  if (result?.ok) {
    $('screen-ready').checked = false;
    requestId = null;
    renderControls();
  }
}
function renderConsoleConnections(card, preference, locked, supported) {
  const modes = preference.modes || [preference.mode];
  card.querySelectorAll('[data-transport]').forEach((label) => {
    const selected = modes.includes(label.dataset.transport),
      input = label.querySelector('input');
    label.classList.toggle('selected', selected);
    input.checked = selected;
    input.dataset.unavailable = String(
      !supported || (selected && modes.length === 1),
    );
    input.disabled = locked || input.dataset.unavailable === 'true';
  });
  const editor = card.querySelector('.address-editor');
  if (!editor.dataset.dual) {
    editor.dataset.dual = 'true';
    const summary = element('summary');
    editor.replaceChildren(summary);
    for (const mode of ['lan', 'wifi']) {
      const form = element('form', 'address-form console-address');
      form.dataset.mode = mode;
      const label = element('label', '', networkName(mode) + ' IP'),
        input = element('input'),
        apply = element('button', '', 'Apply'),
        status = element('p', 'console-path');
      input.type = 'text';
      input.inputMode = 'decimal';
      input.autocomplete = 'off';
      input.placeholder = 'Automatic · optional address';
      input.id = 'console-address-' + mode;
      input.setAttribute(
        'aria-label',
        'Console ' + networkName(mode) + ' IP address',
      );
      label.htmlFor = input.id;
      apply.type = 'submit';
      const auto = element('button', 'quiet', 'Auto');
      auto.type = 'button';
      auto.onclick = () =>
        changeConnection('lan', mode, '', state.connections.devices.lan.modes);
      form.append(label, input, apply, auto, status);
      form.addEventListener('submit', async (e) => {
        e.preventDefault();
        const pref = state.connections.devices.lan;
        await changeConnection(
          'lan',
          mode,
          input.value.trim(),
          pref.modes || [pref.mode],
        );
      });
      editor.append(form);
    }
  }
  const active = preference.paths?.find(
    (p) => p.ready && !p.pending && p.address && p.mode === preference.activeMode,
  );
  setText(editor.querySelector('summary'), active
    ? networkName(active.mode) + ' · ' + active.address + ' · Edit'
    : modes.map(networkName).join(' + ') + ' · Auto / manual');
  for (const form of editor.querySelectorAll('form')) {
    const mode = form.dataset.mode,
      input = form.querySelector('input'),
      path = preference.paths?.find((p) => p.mode === mode),
      enabled = modes.includes(mode),
      status = form.querySelector('.console-path'),
      value =
        preference.addresses[mode] ||
        preference.candidates?.find((p) => p.mode === mode)?.address ||
        '';
    if (document.activeElement !== input) input.value = value;
    const lastKnown=preference.lastKnownAddresses?.[mode];input.placeholder=lastKnown?'Auto · last known '+lastKnown.address:'Automatic · optional address';
    form.querySelectorAll('input,button').forEach((e) => {
      e.dataset.unavailable = String(!supported);
      e.disabled = locked || !supported;
    });
    setText(status, !supported
      ? 'Restart Platform to enable both connections'
      : !enabled
        ? 'Not selected'
        : !path
          ? (lastKnown?'Last known · '+lastKnown.address+' · not checked':'Not checked')
          : path.pending
            ? 'Automatic · waiting for Switch'+(lastKnown?' · last known '+lastKnown.address:'')
            : path.ready
              ? (mode === preference.activeMode ? 'In use' : 'Available') +
                (Number.isFinite(path.latencyMs)
                  ? ' · ' + path.latencyMs.toFixed(2) + ' ms'
                  : '')
              : 'Unavailable');
    status.title = (path?.error?globalThis.IZ2S_ERRORS.canonical(path.error):'') || (lastKnown?'Verified '+lastKnown.verifiedAt+'; current connection not confirmed':'');
    status.classList.toggle('available', !!path?.ready && !path?.pending && enabled);
    status.classList.toggle(
      'active',
      mode === preference.activeMode && !!path?.ready && !path?.pending,
    );
  }
  card.querySelector('.console-paths')?.remove();
}
function openDeviceSettings({tab,addresses=false,focus}={}) {
  const panel=$('hardware-options');
  panel.open=true;
  if(tab)advSetup.select(tab);
  if(addresses)$('hardware-addresses').open=true;
  panel.scrollIntoView({block:'start',behavior:'smooth'});
  (focus ? panel.querySelector(focus) : tab ? $('adv-tab-'+tab) : panel.querySelector('summary'))?.focus({preventScroll:true});
}
function renderArmSetup(meta) {
  const panel = $('arm-setup');
  panel.hidden = meta.configuration.inputAdapter !== 'ssh-gadget';
  if (panel.hidden) return;
  $('arm-manual-command').textContent='sudo python3 '+(meta.loaderId==='civswitch'?'iz2s-civ7-arm-input.run':'iz2s-arm-input.run');
  const source = meta.profiles?.[meta.configurationId] || {};
  const fields = { 'arm-setup-address': source.armAddress || meta.configuration.settings?.armAddress || '', 'arm-setup-user': source.armUser || 'root', 'arm-setup-key': source.sshIdentity || '' };
  if(panel.dataset.configuration!==meta.configurationId){panel.dataset.configuration=meta.configurationId;panel.open=!state.checks.items.find(x=>x.id==='arm')?.ready;}
  for(const [id,value] of Object.entries(fields)) if(document.activeElement!==$(id) && !$(id).dataset.edited) $(id).value=value;
  const ready=state.checks.items.find(x=>x.id==='arm')?.ready;
  $('arm-setup-status').textContent=ready?'Input device ready · setup guide remains available':'Install the input package before the first connection';
  const locked=serverUnavailable||busy||!!meta.locked||!!state.preview;
  panel.querySelectorAll('input,button').forEach(e=>{if(!['arm-method-ssh','arm-method-manual','arm-package-download'].includes(e.id))e.disabled=locked;});
  $('arm-install-target').textContent='Target folder: '+(meta.configuration.inputDirectory||'/opt/iz2s/arm-input')+' · service: iz2s-discovery.service';
  $('arm-install').disabled ||= !$('arm-install-confirm').checked;
}
function renderDeviceDetail(container, row) {
  const detail = String(row.detail || 'Not checked');
  if (!container.querySelector('.detail-text')) {
    const text = element('span', 'detail-text');
    const help = element('button', 'detail-help', '?');
    help.type = 'button';
    const tooltip = element('span', 'detail-tooltip');
    tooltip.id = 'device-help-' + row.id;
    tooltip.setAttribute('role', 'tooltip');
    help.setAttribute('aria-describedby', tooltip.id);
    help.setAttribute('aria-label', 'Connection details');
    help.setAttribute('aria-expanded', 'false');
    help.append(tooltip);
    help.onclick = (event) => {
      event.stopPropagation();
      const open = help.getAttribute('aria-expanded') !== 'true';
      document.querySelectorAll('.detail-help[aria-expanded="true"]').forEach(button => button.setAttribute('aria-expanded', 'false'));
      help.setAttribute('aria-expanded', String(open));
    };
    help.onkeydown = (event) => {
      if (event.key === 'Escape') { help.setAttribute('aria-expanded', 'false'); help.blur(); }
    };
    container.replaceChildren(text, help);
  }
  const extended = detail.length > 70 || detail.includes('\n');
  const first = detail.split(/\.(?:\s|$)|\n/)[0];
  const connectionLost=row.id==='lan'&&globalThis.IZ2S_PRESENTATION.session(state).offline;
  const civPending=row.id==='lan'&&row.pending&&['civswitch','civswitch-ldn'].includes(state?.platform?.loaderId)&&!state.connected;
  setText(container.querySelector('.detail-text'), connectionLost?'Connection lost. Restore the connection to manage applications.':civPending?(state.platform.loaderId==='civswitch-ldn'?'Waiting for the game loader after LDN delivery.':'Wireless lobby may pause LAN. The loader is checked after input.'):row.id === 'lan' && state?.connected ? 'Console connected' : row.id === 'lan' && row.pending ? (state?.active?.launchStage === 'initializing' && state?.link?.state === 'connected' ? 'Console connected · preparing launcher' : state?.active ? 'Waiting for a Switch session.' : 'Start the launcher below to connect Switch.') : extended
    ? (first.length <= 55 ? first.replace(/[.;]$/, '') + '.' : row.pending ? 'Waiting for connection.' : row.ready ? 'Ready.' : 'Setup required.')
    : detail);
  container.querySelector('.detail-help').hidden = !extended;
  setText(container.querySelector('.detail-tooltip'), detail);
}
document.addEventListener('click', (event) => {
  if (!event.target.closest('.detail-help')) document.querySelectorAll('.detail-help[aria-expanded="true"]').forEach(button => button.setAttribute('aria-expanded', 'false'));
  document.querySelectorAll('.device-picker[open]').forEach(picker => {
    if (!picker.contains(event.target)) picker.open = false;
  });
  const files = $('connection-files');
  if (files.open && !files.contains(event.target)) files.open = false;
});
$('connection-files').addEventListener('keydown', (event) => {
  if (event.key === 'Escape') {
    $('connection-files').open = false;
    $('connection-files').querySelector('summary').focus();
  }
});
function renderDevicePicker(card, row) {
  if (!['server', 'arm'].includes(row.id)) return;
  if(row.id==='server'){
    card.querySelector('.device-picker')?.remove();
    card.querySelector('h3').hidden=false;
    setText(card.querySelector('h3'),state.platform.host?.architecture==='arm64'?'Server ARM64PC':'Server x64 PC');
    return;
  }
  const meta = state.platform,
    architecture = meta.host?.architecture,
    serverName = architecture === 'arm64' ? 'Server ARM64PC' : architecture === 'x64' ? 'Server x64 PC' : 'Server',
    inputName = inputDesign === 'own' ? 'Your input device' : meta.configuration.inputAdapter === 'ssh-gadget' ? 'ARM PC' : meta.configuration.hardware?.input?.name || 'ARM PC',
    choices = row.id === 'server'
      ? [
          { id: 'x64', name: 'Server x64 PC', note: architecture === 'x64' ? 'Current server' : 'Start IZ2S on an x64 PC', available: architecture === 'x64' },
          { id: 'arm64', name: 'Server ARM64PC', note: architecture === 'arm64' ? 'Current server' : 'Start IZ2S on an ARM64 PC', available: architecture === 'arm64' },
        ]
      : [
          { id: 'arm', name: 'ARM PC', note: 'USB keyboard input', available: !state.active && !busy },
          { id: 'cardputer', name: 'Cardputer ADV', note: 'Wi-Fi → USB keyboard', available: !state.active && !busy },
          ...(state.preview ? [{ id: 'own', name: 'Manual input · preview', note: 'Interface demonstration only', available: !state.active && !busy }] : []),
        ];
  let picker = card.querySelector('.device-picker');
  if (!picker) {
    picker = element('details', 'device-picker');
    const summary = element('summary');
    summary.setAttribute('aria-label', row.id === 'server' ? 'Server device' : 'Choose an input device');
    const label = element('span', 'device-picker-label'), chevron = element('span', 'device-picker-chevron', '⌄');
    chevron.setAttribute('aria-hidden', 'true');
    summary.append(label, chevron);
    const options = element('div', 'device-picker-options');
    options.setAttribute('role', 'group');
    options.setAttribute('aria-label', row.id === 'server' ? 'Server devices' : 'Input devices');
    picker.append(summary, options);
    picker.addEventListener('keydown', event => {
      if (event.key === 'Escape') { picker.open = false; summary.focus(); }
    });
    picker.addEventListener('toggle', () => {
      if (picker.open) document.querySelectorAll('.device-picker[open]').forEach(other => {
        if (other !== picker) other.open = false;
      });
    });
    card.querySelector('h3').after(picker);
  }
  card.querySelector('h3').hidden = true;
  setText(picker.querySelector('.device-picker-label'), row.id === 'server' ? serverName : inputName);
  const stamp = JSON.stringify([choices, inputDesign, meta.configurationId]);
  if (picker.dataset.choices === stamp) return;
  picker.dataset.choices = stamp;
  const options = picker.querySelector('.device-picker-options');
  options.replaceChildren();
  for (const choice of choices) {
    const button = element('button', 'device-picker-option');
    button.type = 'button';
    button.disabled = !choice.available;
    button.setAttribute('aria-label', choice.name + ' · ' + choice.note);
    const selected = row.id === 'server' ? choice.available : choice.id === (inputDesign === 'own' ? 'own' : meta.configurationId === 'pc-cardputer-adv' ? 'cardputer' : 'arm');
    button.setAttribute('aria-pressed', String(selected));
    const text = element('span');
    text.append(element('strong', '', choice.name), element('small', '', choice.note));
    button.append(text, element('span', 'device-picker-mark', selected ? '✓' : ''));
    button.onclick = async () => {
      if (row.id === 'arm') {
        if (choice.id !== 'own') {
          inputDesign = 'arm';
          const configurationId = choice.id === 'cardputer' ? 'pc-cardputer-adv'
            : meta.configuration.inputAdapter === 'ssh-gadget' ? meta.configurationId : 'pc-arm';
          if (configurationId !== meta.configurationId) await act('/api/launch-settings', {configurationId});
        } else inputDesign = choice.id;
        manualDesignStage = 1;
        manualDesignConfirmed.clear();
        $('manual-fs-preview').checked = state.platform.loaderSettings?.fsRpcMode === true;
        renderDevices();
        renderControls();
      }
      picker.open = false;
      picker.querySelector('summary').focus();
      if(row.id==='arm'&&choice.id!=='own')openDeviceSettings({tab:choice.id==='cardputer'?1:undefined});
    };
    options.append(button);
  }
}
function renderManualInputDesign() {
  const meta = state?.platform;
  if (!meta) return;
  if (state.active || state.emergencyInputPending || !meta.izModules) inputDesign = 'arm';
  const shown = inputDesign === 'own', wasShown = !$('manual-input-design').hidden;
  $('manual-input-design').hidden = !shown;
  if (shown) $('iz-flow').hidden = true;
  else if (wasShown) {
    $('iz-flow').hidden = !meta.izModules||['civswitch','civswitch-ldn'].includes(meta.loaderId);
    renderEngineInstall();
  }
  const arm = $('device-arm');
  // One owner controls tile visibility in every render path, including the
  // busy frame before Restore connection returns a new status snapshot.
  renderDeviceTopology();
  if (arm) {
    arm.classList.toggle('manual-preview', shown);
    let note = $('manual-device-description');
    if (!note) {
      note = element('p', 'manual-device-description', 'Your input method · Connection is not checked.');
      note.id = 'manual-device-description';
      arm.querySelector('.device-heading').append(note);
    }
    note.hidden = !shown;
    const row = state.checks.items.find(item => item.id === 'arm');
    if (row) renderDevicePicker(arm, row);
  }
  document.querySelector('.network-caption').hidden = shown;
  $('hardware-options').hidden = shown || meta.loaderId==='civswitch-ldn';
  if (!shown) return;
  $('arm-setup').hidden = true;
  $('cardputer-setup').hidden = true;
  $('engine-install').hidden = true;
  $('installation-options').hidden = true;
  $('recheck').disabled = true;
  document.querySelectorAll('.transport-choice input,.transport-choice button,.address-form input,.address-form select,.address-form button,.detected-peer')
    .forEach(control => { control.disabled = true; });
  document.querySelectorAll('[data-variant],[data-loader]').forEach(button => { button.disabled = true; });
  const withFiles = $('manual-fs-preview').checked, total = withFiles ? 7 : 6;
  document.querySelector('.manual-design-steps').style.setProperty('--manual-step-count', total);
  if (manualDesignStage > total) manualDesignStage = total;
  $('manual-files-step').hidden = !withFiles;
  const stages = {
    1: ['Preparation', 'This previews the manual input interface. Buttons only change the preview stage.'],
    2: ['Confirm manual input', 'The panel does not read or verify the entered data.', 'I checked that the manually entered data is correct.'],
    3: ['Verification', 'Follow the selected engine instructions to verify the result yourself.'],
    4: ['Confirm save', 'This switch records your confirmation in the preview.', 'I checked that the data was saved correctly.'],
    5: ['Continue', 'The next preview stage. No commands are sent to the device.'],
    6: ['Confirm result', 'You verify the result. This preview cannot confirm the device state.', 'I checked the result and it is correct.'],
    7: ['Filesystem', 'Open an empty file manager preview in another tab. Device connections and file reads are disabled.'],
  };
  const [title, description, confirmation] = stages[manualDesignStage];
  setText($('manual-stage-counter'), 'STAGE ' + manualDesignStage + ' / ' + total);
  setText($('manual-stage-title'), title);
  setText($('manual-stage-description'), description);
  $('manual-confirm-row').hidden = !confirmation;
  setText($('manual-confirm-label'), confirmation || '');
  $('manual-stage-confirm').checked = manualDesignConfirmed.has(manualDesignStage);
  $('manual-next').disabled = !!confirmation && !manualDesignConfirmed.has(manualDesignStage);
  $('manual-next').hidden = manualDesignStage === 7;
  $('manual-files-link').hidden = manualDesignStage !== 7;
  setText($('manual-next'), manualDesignStage === total ? 'Return to the beginning' : 'Next stage');
  setText($('manual-engine-folder'), meta.loaders.find(loader => loader.id === meta.loaderId)?.folder || 'Find this path in Files → Engine.');
  document.querySelectorAll('[data-manual-stage]').forEach(button => {
    const selected = Number(button.dataset.manualStage) === manualDesignStage;
    button.classList.toggle('selected', selected);
    if (selected) button.setAttribute('aria-current', 'step');
    else button.removeAttribute('aria-current');
  });
}
document.querySelectorAll('[data-manual-stage]').forEach(button => {
  button.onclick = () => { manualDesignStage = Number(button.dataset.manualStage); renderManualInputDesign(); };
});
$('manual-fs-preview').onchange = renderManualInputDesign;
$('manual-stage-confirm').onchange = () => {
  if ($('manual-stage-confirm').checked) manualDesignConfirmed.add(manualDesignStage);
  else manualDesignConfirmed.delete(manualDesignStage);
  renderManualInputDesign();
};
$('manual-next').onclick = () => {
  if (inputDesign !== 'own' || state?.active || state?.emergencyInputPending || $('manual-next').disabled) return;
  if (manualDesignStage !== 7) { manualDesignStage = manualDesignStage === 6 ? ($('manual-fs-preview').checked ? 7 : 1) : manualDesignStage + 1; renderManualInputDesign(); }
};
$('manual-exit').onclick = () => { inputDesign = 'arm'; renderDevices(); renderControls(); };
function retainDeviceControls(rows=state.checks.items){
  for(const card of $('devices').children||[]){card.classList.remove('civ-run-console');card.classList.remove('awaiting-loader');}
  for(const prefix of ['civ','ldn']){const endpoint=$(prefix+'-run-endpoint'),home=$(prefix+'-run-connection'),selected=state.platform?.loaderId==='civswitch-ldn'?'ldn':state.platform?.loaderId==='civswitch'?'civ':'';if(endpoint&&home&&$('devices').contains(endpoint)&&(prefix!==selected||!state.checks.items.some(row=>row.id==='lan')))home.append(endpoint);}
  const confirmation=$('adv-usb-next'),devices=$('devices');
  // This shared control lives in the USB tile while it exists. Keep it alive
  // when changing to a game that has no USB tile.
  if(confirmation&&devices.contains(confirmation)&&!rows.some(row=>row.id==='usb'))devices.after(confirmation);
}
function renderDeviceConnections(){
 const devices=$('devices');if(!devices)return;
 devices.classList.add('network-topology');
 const cards=[...devices.children].filter(card=>!card.hidden&&card.offsetWidth>0).map(card=>({x:card.offsetLeft,y:card.offsetTop,width:card.offsetWidth,height:card.offsetHeight,tone:card.classList.contains('connection-lost')?'lost':card.classList.contains('ready')&&!card.classList.contains('pending')?'ready':'waiting'}));
 const width=devices.clientWidth,height=Math.max(0,...cards.map(card=>card.y+card.height)),links=globalThis.IZ2S_PRESENTATION.deviceLinks(cards);
 const stamp=JSON.stringify([width,height,links]);if(devices.dataset.connections===stamp)return;devices.dataset.connections=stamp;
 const colors={ready:'#8bd0ff',waiting:'#d6b05a',lost:'#ef9494'};
 const content=links.map(link=>{const color=colors[link.tone];return `<path d="${link.d}" fill="none" stroke="${color}" stroke-opacity=".55"/><circle cx="${link.x}" cy="${link.y}" r="5" fill="#171d19" stroke="${color}" stroke-opacity=".5"/><circle cx="${link.x}" cy="${link.y}" r="2.5" fill="${color}"/>`;}).join('');
 const svg=`<svg xmlns="http://www.w3.org/2000/svg" width="${width}" height="${height}" viewBox="0 0 ${width} ${height}">${content}</svg>`;
 devices.style.setProperty('--network-paths',links.length?`url("data:image/svg+xml,${encodeURIComponent(svg)}")`:'none');
}
// Change the topology only at the handoff from input hardware to networking.
// Repeated status polls keep the same DOM and never replay this transition.
function renderDeviceTopology(){
 const devices=$('devices'),direct=globalThis.IZ2S_PRESENTATION.directNetwork(state),mode=direct?'direct':'input';
 const changed=devices.dataset.topology!==mode,previous=devices.dataset.topology;
 const animate=changed&&previous&&devices.getBoundingClientRect().height>0&&!window.matchMedia('(prefers-reduced-motion: reduce)').matches;
 const before=animate?devices.getBoundingClientRect():null,positions=new Map();
 if(animate)for(const card of devices.children)if(!card.hidden)positions.set(card.id,card.getBoundingClientRect());
 if(changed){devices.getAnimations().forEach(animation=>animation.cancel());for(const card of devices.children)card.getAnimations().forEach(animation=>animation.cancel());}
 devices.classList.toggle('direct-network',direct);devices.classList.toggle('network-interrupted',direct&&globalThis.IZ2S_PRESENTATION.session(state).offline);
 for(const id of ['arm','usb']){const card=$('device-'+id);if(!card)continue;const hidden=direct||id==='usb'&&inputDesign==='own';if(hidden&&card.contains(document.activeElement))$('recheck').focus({preventScroll:true});card.hidden=hidden;}
 devices.style.setProperty('--device-count',[...devices.children].filter(card=>!card.hidden).length);
 devices.dataset.topology=mode;
 if(!renderDeviceTopology.observer&&typeof ResizeObserver==='function'){renderDeviceTopology.observer=new ResizeObserver(renderDeviceConnections);renderDeviceTopology.observer.observe(devices);}
 if(!animate){renderDeviceConnections();return;}
 const after=devices.getBoundingClientRect(),timing={duration:280,easing:'cubic-bezier(.2,.75,.25,1)'};
 devices.animate([{height:before.height+'px'},{height:after.height+'px'}],timing);
 for(const card of devices.children){if(card.hidden)continue;const old=positions.get(card.id),next=card.getBoundingClientRect();
  card.animate(old?[{transformOrigin:'top left',transform:`translate(${old.x-next.x}px,${old.y-next.y}px) scale(${old.width/next.width},${old.height/next.height})`,opacity:.75},{transformOrigin:'top left',transform:'none',opacity:1}]:[{opacity:0,transform:'translateY(6px)'},{opacity:1,transform:'none'}],timing);
 }
 renderDeviceConnections();
}
function renderDevices() {
  const rows=state.platform?.loaderId==='civswitch-ldn'?state.checks.items.filter(row=>['server','lan','ldn'].includes(row.id)):state.checks.items;
  retainDeviceControls(rows);
  const config = state.connections;
  $('devices').classList.toggle('ldn-devices',state.platform?.loaderId==='civswitch-ldn');
  for(const card of [...$('devices').children])if(!rows.some(row=>card.id==='device-'+row.id))card.remove();
  $('devices').style.setProperty('--device-count',rows.length);
  for (const [index, row] of rows.entries()) {
    const deviceInfo = words[row.id] || [row.label || 'Device', document.getElementById(row.icon)?.tagName.toLowerCase() === 'symbol' ? row.icon : 'i-server'];
    let card = $('device-' + row.id);
    const preference = config?.devices?.[row.id];
    if (!card) {
      card = element('article', 'device');
      card.id = 'device-' + row.id;
      const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg'),
        use = document.createElementNS('http://www.w3.org/2000/svg', 'use');
      use.setAttribute('href', '#' + deviceInfo[1]);
      svg.setAttribute('aria-hidden', 'true');
      svg.append(use);
      const heading = element('div', 'device-heading');
      heading.append(
        svg,
        element('span', 'state'),
        element('h3', '', deviceInfo[0]),
        element('p', 'device-detail'),
        element('p', 'device-ping'),
      );
      card.append(heading);
      if (row.id !== 'usb') {
        const group = element('div', 'transport-choice');
        group.setAttribute('role', 'group');
        group.setAttribute(
          'aria-label',
          deviceInfo[0] + ' network connection',
        );
        for (const mode of ['lan', 'wifi']) {
          const button = element(
            row.id === 'lan' ? 'label' : 'button',
            'transport-option',
          );
          button.dataset.transport = mode;
          button.title = networkName(mode);
          if (row.id === 'lan') {
            const input = element('input');
            input.type = 'checkbox';
            input.setAttribute(
              'aria-label',
              'Enable console ' + networkName(mode),
            );
            input.addEventListener('change', () => {
              const current = state.connections.devices.lan.modes || [
                  state.connections.devices.lan.mode,
                ],
                modes = input.checked
                  ? [...new Set([...current, mode])]
                  : current.filter((m) => m !== mode);
              if (!modes.length) {
                input.checked = true;
                return;
              }
              changeConnection(row.id, undefined, undefined, modes);
            });
            button.append(input, element('span', 'transport-tick', '✓'));
          } else {
            button.type = 'button';
            button.setAttribute('aria-label', networkName(mode));
            button.addEventListener('click', () =>
              changeConnection(row.id, mode),
            );
          }
          const icon = document.createElementNS(
              'http://www.w3.org/2000/svg',
              'svg',
            ),
            symbol = document.createElementNS(
              'http://www.w3.org/2000/svg',
              'use',
            );
          symbol.setAttribute('href', mode === 'wifi' ? '#i-wifi' : '#i-lan');
          icon.setAttribute('aria-hidden', 'true');
          icon.append(symbol);
          button.append(icon);
          group.append(button);
        }
        const editor = element('details', 'address-editor'),
          summary = element('summary'),
          form = element('form', 'address-form'),
          label = element(
            'label',
            '',
            row.id === 'server' ? 'Listen on this PC' : 'Device IP address',
          );
        const input = element(row.id === 'server' ? 'select' : 'input');
        input.id = 'address-' + row.id;
        input.setAttribute('aria-label', deviceInfo[0] + ' IP address');
        if (row.id !== 'server') {
          input.type = 'text';
          input.inputMode = 'decimal';
          input.autocomplete = 'off';
          input.placeholder = 'Automatic · or enter IP';
          const list = element('datalist');
          list.id = 'addresses-' + row.id;
          input.setAttribute('list', list.id);
          form.append(list);
        }
        const apply = element('button', '', 'Apply');
        apply.type = 'submit';
        label.htmlFor = input.id;
        const automatic = element('button', 'quiet', 'Auto');
        automatic.type = 'button';
        automatic.onclick = () =>
          act('/api/connection', {
            id: row.id,
            mode: state.connections.devices[row.id].mode,
            automatic: true,
          });
        form.append(label, input, apply, automatic);
        form.addEventListener('submit', async (event) => {
          event.preventDefault();
          const pref = state.connections?.devices[row.id];
          if (!pref) return;
          await changeConnection(row.id, pref.mode, input.value.trim());
        });
        const suggestions = element('div', 'detected-peers');
        editor.append(summary, suggestions, form);
        card.append(group, editor);
      } else {
        const fixed = element('div', 'fixed-transport');
        fixed.title = 'USB · direct cable';
        fixed.setAttribute('aria-label', 'USB · direct cable');
        const usb = svg.cloneNode(true);
        fixed.append(usb);
        card.append(fixed);
      }
    }
    placeChild($('devices'), card, index);
    setText(card.querySelector('h3'), row.label || deviceInfo[0]);
    renderDevicePicker(card, row);
    const connectionLost=row.id==='lan'&&globalThis.IZ2S_PRESENTATION.session(state).offline;
    card.classList.toggle('connection-lost',connectionLost);
    card.classList.toggle('ready', row.ready&&!connectionLost);
    card.classList.toggle('pending', !!row.pending && !connectionLost && !(row.id === 'lan' && state?.connected));
    setText(card.querySelector('.state'), connectionLost?'CONNECTION LOST':row.id === 'lan' && state?.connected ? '✓ READY' : row.ready
      ? row.pending
        ? (row.id === 'lan' && state?.active?.launchStage === 'initializing' && state?.link?.state === 'connected' ? 'INITIALIZING' : row.id === 'lan' && !state?.active ? 'START SESSION' : 'WAITING')
        : '✓ READY'
      : state.preview
        ? 'PREVIEW'
        : '○ WAITING');
    if(row.id==='usb'&&state.platform?.configuration?.inputAdapter==='cardputer-wifi'){
      const connection=card.querySelector('.fixed-transport');
      connection.classList.add('usb-connection-actions');
      if($('adv-usb-next').parentElement!==connection)connection.append($('adv-usb-next'));
    }
    const detailRow = row.id === 'usb' && state.platform?.configuration?.inputAdapter === 'cardputer-wifi'
      ? {...row, detail: globalThis.IZ2S_ADV_SETUP.usbDescription(state, state.checks.items.find(item => item.id === 'arm'), row)}
      : row;
    renderDeviceDetail(card.querySelector('.device-detail'), detailRow);
    let settingsButton=card.querySelector('.device-setup-button');
    const adv=state.platform?.configuration?.inputAdapter==='cardputer-wifi';
    const configurable=row.id==='arm'&&inputDesign!=='own'||row.id==='usb'&&adv;
    if(configurable&&!settingsButton){
      settingsButton=element('button','device-setup-button quiet');settingsButton.type='button';
      settingsButton.onclick=()=>openDeviceSettings({tab:state.platform.configuration.inputAdapter==='cardputer-wifi'?(row.id==='usb'?3:1):undefined});
      card.append(settingsButton);
    }
    if(settingsButton){
      settingsButton.hidden=!configurable;
      settingsButton.textContent=row.id==='usb'?'USB settings':adv?'Setup and firmware':'Device setup';
      settingsButton.disabled=serverUnavailable;
    }
    const ping = card.querySelector('.device-ping');
    ping.hidden = !Number.isFinite(row.latencyMs);
    setText(ping, Number.isFinite(row.latencyMs)
      ? 'Last ping · ' + row.latencyMs.toFixed(2) + ' ms'
      : '\u00a0');
    const transport=card.querySelector('.transport-choice');
    if(transport){
      transport.hidden=row.id==='ldn';
      for(const option of transport.children)option.hidden=option.classList.contains('ldn-transport');
    }
    if(state.platform.loaderId==='civswitch-ldn'){
      if(transport){
        let badge=transport.querySelector('.ldn-transport');
        if(!badge){badge=element('span','ldn-transport');badge.append(transport.querySelector('[data-transport="wifi"] svg').cloneNode(true),element('span','','Wi-Fi · LDN'));transport.append(badge);}
        for(const option of transport.children)option.hidden=option!==badge;
      }
      const editor=card.querySelector('.address-editor');if(editor)editor.hidden=true;continue;
    }
    if (preference) {
      const locked = busy || !!config.locked || !!state.preview;
      const cardputer = row.id === 'arm' && state.platform.configuration.inputAdapter === 'cardputer-wifi';
      const addressEditor = card.querySelector('.address-editor');
      addressEditor.hidden = cardputer;
      let cardputerAddress = card.querySelector('.cardputer-address');
      if (cardputer && !cardputerAddress) {
        cardputerAddress = element('button', 'cardputer-address quiet');
        cardputerAddress.type = 'button';
        cardputerAddress.onclick = () => {
          openDeviceSettings({addresses:true,focus:'[name="cardputerEndpoint"]'});
        };
        card.append(cardputerAddress);
      }
      if (cardputerAddress) cardputerAddress.hidden = !cardputer;
      card.querySelectorAll('[data-transport]').forEach(button => { button.hidden = cardputer && button.dataset.transport !== 'wifi'; button.dataset.fixed = String(cardputer); });
      if (cardputer) {
        const wifi = card.querySelector('[data-transport="wifi"]');
        wifi.classList.add('selected');
        wifi.setAttribute('aria-pressed', 'true');
        wifi.disabled = true;
        const settings = state.platform.profiles?.[state.platform.configurationId] || {};
        setText(cardputerAddress, (settings.cardputerEndpoint || 'http://iz2s-adv.local') + ' · Edit');
        cardputerAddress.disabled = locked;
        continue;
      }
      if (row.id === 'lan') {
        renderConsoleConnections(
          card,
          preference,
          locked,
          config.dualConsoleSupported,
        );
        card
          .querySelector('.device-heading use')
          .setAttribute(
            'href',
            '#' +
              (preference.activeMode === 'wifi' ||
              (preference.modes?.length !== 2 && preference.mode === 'wifi')
                ? 'i-wifi'
                : 'i-lan'),
          );
        continue;
      }
      card.querySelectorAll('[data-transport]').forEach((button) => {
        const selected = button.dataset.transport === preference.mode;
        button.classList.toggle('selected', selected);
        button.setAttribute('aria-pressed', String(selected));
        button.disabled = locked;
      });
      const icon =
        row.id === 'lan'
          ? preference.mode === 'wifi'
            ? 'i-wifi'
            : 'i-lan'
          : deviceInfo[1];
      card.querySelector('use').setAttribute('href', '#' + icon);
      const editor = card.querySelector('.address-editor'),
        input = $('address-' + row.id);
      setText(editor.querySelector('summary'), preference.address
        ? (preference.automatic ? 'Auto · ' : '') +
          preference.address +
          ' · Edit'
        : 'Auto / manual · ' + networkName(preference.mode));
      if (document.activeElement !== input) {
        if (row.id === 'server') {
          const options = preference.options.filter(
              (o) => o.mode === preference.mode,
            ),
            stamp = JSON.stringify([
              options,
              preference.address,
              preference.automatic,
            ]);
          if (input.dataset.stamp !== stamp) {
            input.dataset.stamp = stamp;
            input.replaceChildren();
            const automatic = element('option', '', 'Automatic');
            automatic.value = '';
            input.append(automatic);
            if (
              preference.address &&
              !options.some((o) => o.address === preference.address)
            ) {
              const option = element(
                'option',
                '',
                preference.address ||
                  'No active ' + networkName(preference.mode) + ' adapter',
              );
              option.value = preference.address;
              input.append(option);
            }
            for (const item of options) {
              const option = element(
                'option',
                '',
                item.address + ' · ' + item.name,
              );
              option.value = item.address;
              input.append(option);
            }
            input.value = preference.automatic ? '' : preference.address;
          }
        } else if (input.dataset.mode !== preference.mode || !editor.open)
          input.value = preference.address;
        input.dataset.mode = preference.mode;
      }
      if (row.id !== 'server') {
        const list = $('addresses-' + row.id),
          stamp = JSON.stringify(preference.options);
        if (list.dataset.stamp !== stamp) {
          list.dataset.stamp = stamp;
          list.replaceChildren();
          for (const item of preference.options) {
            const option = element('option');
            option.value = item.address;
            option.label = item.name || item.address;
            list.append(option);
          }
        }
      }
      const suggestions = editor.querySelector('.detected-peers'),
        choices =
          row.id === 'server'
            ? []
            : preference.options.filter(
                (p) => p.address !== state.connections.devices.server.address,
              ),
        choiceStamp = JSON.stringify([preference.mode, choices]);
      if (suggestions.dataset.stamp !== choiceStamp) {
        suggestions.dataset.stamp = choiceStamp;
        suggestions.replaceChildren();
        if (choices.length) {
          suggestions.append(
            element('span', '', 'Detected on ' + networkName(preference.mode)),
          );
          for (const item of choices.slice(0, 5)) {
            const choose = element(
              'button',
              'detected-peer',
              item.name ? item.name + ' · ' + item.address : item.address,
            );
            choose.type = 'button';
            choose.addEventListener('click', () =>
              changeConnection(
                row.id,
                state.connections.devices[row.id].mode,
                item.address,
              ),
            );
            suggestions.append(choose);
          }
        }
      }
      input.disabled = locked;
      editor
        .querySelectorAll('button')
        .forEach((button) => (button.disabled = locked));
    } else if (row.id !== 'usb') {
      if(row.id==='ldn'){
        card.querySelector('.transport-choice').hidden=true;
        card.querySelector('.address-editor').hidden=true;
      }
      card
        .querySelectorAll('button,input,select')
        .forEach((e) => (e.disabled = true));
    }
  }
  // Give every endpoint the same header / connection / settings rows.
  for(const card of $('devices').children){
    const heading=card.querySelector('.device-heading'),connection=card.querySelector('.transport-choice,.fixed-transport');
    let controls=card.querySelector('.device-controls');
    if(!controls){controls=element('div','device-controls');card.append(controls);}
    for(const node of [...card.children])if(node!==heading&&node!==connection&&node!==controls)controls.append(node);
    const editor=card.querySelector('.address-editor');
    if(editor&&!editor.querySelector('.address-popover')){
      const content=element('div','address-popover');
      for(const node of [...editor.children])if(node.tagName!=='SUMMARY')content.append(node);
      editor.append(content);
    }

  }
  const consoleState = state.consoleState,
    known = state.connected && consoleState && !consoleState.stale;
  const modeLine = $('console-mode-state');
  modeLine.hidden = !known;
  if (known) {
    const mode =
        consoleState.mode === 'docked'
          ? 'TV mode'
          : consoleState.mode === 'handheld'
            ? 'Portable mode'
            : 'Output mode unknown',
      network =
        consoleState.network.type === 'ethernet'
          ? 'Ethernet'
          : consoleState.network.type === 'wifi'
            ? 'Wi-Fi'
            : 'network unknown',
      display = consoleState.display;
    modeLine.textContent =
      mode +
      ' · ' +
      display.width +
      ' × ' +
      display.height +
      ' · ' +
      network +
      (consoleState.network.connected ? '' : ' (disconnected)') +
      (consoleState.warning ? ' · Ethernet is not connected.' : '');
    modeLine.classList.toggle('warning', !!consoleState.warning);
  }
  const modes = config?.devices;
  $('network-summary').textContent = modes
    ? ['server', 'arm', 'lan'].filter(id=>state.checks.items.some(row=>row.id===id))
        .map(
          (id) =>
            (state.checks.items.find(row=>row.id===id)?.label||words[id][0]) +
            ' ' +
            (id === 'arm' && state.platform.configuration.inputAdapter === 'cardputer-wifi' ? ['wifi'] : modes[id].modes || [modes[id].mode]).map(networkName).join(' + '),
        )
        .join('  ·  ')
    : 'Restart Platform to enable connection choices.';
  $('network-help').textContent = state.platform.loaderId==='civswitch-ldn'
    ? 'CIV7 LAN uses the PC Wi-Fi radio for LDN. ADV and USB input are not required. Ethernet stays connected.'
    : config?.locked
    ? 'Connections are locked for this session. Stop it before changing networks.'
    : state.platform.configuration.inputAdapter === 'cardputer-wifi'
      ? 'Cardputer receives commands over Wi-Fi and types on Switch over USB. Use the IP shown on its screen.'
    : config?.discovery?.ambiguous
      ? 'Several ARM services found. Choose the required device, or enter its IP.'
      : 'Automatic discovery uses your local network. Manual addresses take priority.';
}

function residentEligibility(pkg) {
  return state?.platform?.residentFiniteAudio ? pkg?.residentAudio : pkg?.resident;
}
function selectedShellName() {
  const id=['civswitch','civswitch-ldn'].includes(state?.platform?.loaderId)?state.civLauncher?.shell||'izlauncher':state?.platform?.variant;
  return id==='original'?'Core only':state?.platform?.shells?.find(shell=>shell.id===id)?.name||(id==='izpslauncher'?'IZPSlauncher':'IZlauncher');
}

function renderStartupModules() {
  syncStartupChoices();
  const resident=state.platform?.residentGraphics===true&&['izlauncher','izpslauncher'].includes(state.platform?.variant);
  const stamp = JSON.stringify(
    [resident,state.moduleErrors,state.platform?.sdLauncher,state.platform?.residentFiniteAudio,state.library.map((p) => [p.id, p.name, p.version, p.icon, p.supported,p.resident,p.residentAudio,p.session])],
  );
  if (stamp === startupStamp) return;
  startupStamp = stamp;
  const valid = new Set(state.library.filter(p => p.supported !== false&&p.session?.compatible!==false).map(p => p.id));
  for (const id of startupSelection)
    if (!valid.has(id)) startupSelection.delete(id);
  $('preload-list').replaceChildren();
  if(!state.library.length)$('preload-list').append(element('p','hint',state.moduleErrors?.length?'Could not read the modules. Open App library for details.':'No modules installed. Open App library to add applications.'));
  for (const pkg of state.library) {
    const eligibility=residentEligibility(pkg);
    const label = element('label', 'preload-item'),
      input = element('input'),
      image = element('img'),
      text = element('span');
    input.type = 'checkbox';
    input.checked = startupSelection.has(pkg.id);
    input.dataset.unavailable = String(pkg.supported === false);
    input.dataset.residentUnavailable=String(resident&&eligibility?.compatible!==true);
    input.disabled = pkg.supported === false || pkg.session?.compatible===false || (resident&&eligibility?.compatible!==true&&!input.checked);
    if(pkg.session?.compatible===false)input.dataset.unavailable='true';
    if(resident&&eligibility?.compatible!==true)label.title=eligibility?.userReason||'Restart the panel to check compatibility.';
    input.setAttribute('aria-label', 'Load ' + pkg.name + ' at startup');
    input.addEventListener('change', () => {
      input.checked
        ? startupSelection.add(pkg.id)
        : startupSelection.delete(pkg.id);
      saveStartup();
      renderControls();
    });
    image.src = pkg.icon;
    image.alt = '';
    text.append(
      element('strong', 'app-name', pkg.name),
      element('small', '', pkg.version),
    );
    if(pkg.session?.compatible===false)text.append(element('small','',pkg.session.reason||'Unavailable for this game'));
    else if(resident)text.append(element('small','',eligibility?.userReason||'Compatibility has not been checked.'));
    label.append(input, image, text);
    $('preload-list').append(label);
  }
}
$('preload-enabled').addEventListener('change', () => {
  saveStartup();
  renderControls();
});
$('preload-clear').addEventListener('click', () => {
  startupSelection.clear();
  document
    .querySelectorAll('#preload-list input')
    .forEach((e) => (e.checked = false));
  saveStartup();
  renderControls();
});
let libraryContext=null;
function leaveLibraryContext(){
  if(!libraryContext)return;const previous=libraryContext;libraryContext=null;
  $('library-context').hidden=true;$('library-search').value=previous.query;$('library-status').value=previous.status;$('library-location').value=previous.location;
  return previous;
}
function openConsoleApplication(app){
  libraryContext={id:app.id,name:app.name,query:$('library-search').value,status:$('library-status').value,location:$('library-location').value,scroll:window.scrollY};
  switchView('apps',{focusApp:true});$('library-search').value='';$('library-status').value='all';$('library-location').value='all';
  $('library-context').hidden=false;$('library-context-name').textContent=app.name;renderLibrary();$('library-back').focus({preventScroll:true});
}
$('library-back').onclick=()=>{const previous=leaveLibraryContext();switchView('setup');if(previous){window.scrollTo({top:previous.scroll,left:0,behavior:'auto'});document.querySelector('#console-apps [data-app-id="'+CSS.escape(previous.id)+'"]')?.focus({preventScroll:true});}};
$('library-context-all').onclick=()=>{leaveLibraryContext();applyLibrarySearch();};
function renderConsoleApps(){
  const box=$('console-apps'),list=$('console-apps-list');if(!box||!list)return;
  const apps=globalThis.IZ2S_PRESENTATION.consoleApps(state);box.hidden=!apps.length;
  const stamp=JSON.stringify(apps);if(list.dataset.stamp===stamp)return;list.dataset.stamp=stamp;list.replaceChildren();
  for(const app of apps){
    const item=element('button','console-app'),img=element('img'),copy=element('span','console-app-copy'),name=element('strong','',app.name),places=element('span','console-app-places');
    item.type='button';item.dataset.appId=app.id;item.title=app.name;img.src=app.icon;img.alt='';name.setAttribute('translate','no');
    for(const [available,label]of [[app.ram,'RAM'],[app.sd,'SD']])if(available){const badge=element('small',label==='SD'?'source-sd':'source-ram',label);badge.setAttribute('translate','no');places.append(badge);}
    copy.append(name,places);item.append(img,copy);item.onclick=()=>openConsoleApplication(app);list.append(item);
  }
}
$('console-apps-library').onclick=()=>switchView('apps');
function renderLibrary() {
  const packages=globalThis.IZ2S_PRESENTATION?.library?.(state)||state.library;
  const errors = $('module-errors');
  if (errors) {
    errors.hidden = !state.moduleErrors?.length;
    errors.setAttribute('translate','no');errors.textContent = (state.moduleErrors || []).map(row => (row.id || row.path || 'Module') + ': ' + globalThis.IZ2S_ERRORS.canonical(row.error)).join(' · ');
  }
  const civ=['civswitch','civswitch-ldn'].includes(state.platform?.loaderId);
  const available = new Set(packages.filter(pkg => pkg.supported !== false&&(!civ||pkg.session?.compatible!==false)).map(pkg => pkg.id));
  for (const id of selection) if (!available.has(id)) selection.delete(id);
  const stamp = JSON.stringify(
    [state.platform?.sdLauncher,packages.map((p) => [p.id, p.version, p.bytes, p.name, p.icon, p.description, p.supported, p.contents, p.sd, p.session])],
  );
  if (stamp !== packageStamp) {
    packageStamp = stamp;
    $('library').replaceChildren();
    if(!packages.length){const empty=element('div','empty');empty.append(element('h2','','No modules installed'),element('p','','Open the Modules folder and add an IZM package.'));$('library').append(empty);}
    $('module-host-actions').dataset.stamp = '';
    for (const pkg of packages) {
      const card = element('article', 'app-card'),
        label = element('label', 'app-select'),
        input = element('input'),
        img = element('img');
      card.dataset.appId = pkg.id;label.title=pkg.name;
      input.type = 'checkbox';
      input.disabled = !available.has(pkg.id);
      if(state.platform?.sdLauncher&&(!pkg.sd?.compatible||pkg.sd?.hostRequired))label.title='Stored on SD; uses the PC executor when launched.';
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
        input,
        img,
        element('strong', 'app-name', pkg.name),
        element(
          'small',
          '',
          (pkg.sdOnly?'SD: ':'PC: ')+(pkg.version||'version unknown') + ' · ' + (pkg.bytes / 1024).toFixed(1) + ' KiB',
        ),
        element('p', 'app-description', pkg.description),
      );
      if(civ&&pkg.session?.compatible===false)label.append(element('small','app-requirement',pkg.session.reason||'Unavailable for this game'));
      else if(pkg.session?.requiresFilesystem)label.append(element('small','app-requirement','Requires filesystem access'));
      card.append(label, element('div', 'module-host-actions'));
      if(state.platform?.sdLauncher&&(!pkg.sd?.compatible||pkg.sd?.hostRequired))card.append(element('p','hint','Stored on SD; uses the PC executor when launched.'));
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
      const actions=element('div','module-actions');card.append(actions);
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
      card.append(storageControls.mount(pkg));
      card.append(element('small','app-memory',''));
      const technical=element('details','app-more'),summary=element('summary','','More'),detailBody=element('div','app-more-body');technical.append(summary,detailBody);detailBody.append(element('p','app-full-description',pkg.description));if(pkg.sdOnly){const location=element('code');location.textContent=pkg.displayPath||'SD:/IZ2S/Homebrew/Modules/'+pkg.id+'/';detailBody.append(location);}
      const detailActions=element('div','app-detail-actions');detailBody.append(detailActions);
      const cardActions=card.querySelector('.module-actions');if(cardActions){for(const button of [...cardActions.children])if(!button.classList.contains('module-configure'))if(!pkg.sdOnly)detailActions.append(button);else button.remove?.();const contents=card.querySelector('.module-contents');if(contents)detailBody.append(contents);cardActions.append(technical);}
      for(const child of [...card.children])if(child.tagName==='P'&&child.classList.contains('hint'))detailBody.append(child);
      card.querySelector('.app-select')?.querySelector('strong')?.setAttribute('translate','no');
      $('library').append(card);
    }
  }
  const sdRefresh=$('library-sd-refresh');sdRefresh.hidden=civ;const sdError=globalThis.IZ2S_STORAGE?.reason?.(state,{id:'sd-inventory'},'sd',{remove:true})||'';sdRefresh.disabled=busy||serverUnavailable||!!sdError;sdRefresh.title=sdError;
  const installed = new Set([...(state.installed||[]),...(state.sdInstalled||[])].map(app => typeof app === 'string' ? app : app.id));
  for (const card of $('library').querySelectorAll('.app-card')) {
    const loaded = installed.has(card.dataset.appId);
    card.classList.toggle('installed', loaded);const presence=globalThis.IZ2S_STORAGE?.installed?.(state,card.dataset.appId)||{session:loaded,sd:false};card.classList.toggle('in-ram',presence.session);card.classList.toggle('on-sd',presence.sd);
    card.dataset.installed = String(loaded);
    card.querySelector('img').title = loaded ? 'Loaded on console' : 'Not loaded on console';
    const retained=state.residentMemory?.apps?.find(app=>app.id===card.dataset.appId),memoryLabel=card.querySelector('.app-memory');
    if(memoryLabel){memoryLabel.hidden=!retained;setText(memoryLabel,retained?(state.connected?'Retained in RAM':'Previously retained')+' · '+(retained.bufferBytes/1048576).toFixed(1)+' MiB':'');}
    storageControls.update(card.querySelector('.app-storage'),packages.find(pkg=>pkg.id===card.dataset.appId));
  }
  renderModuleHostActions();
  setText($('app-count'), packages.length);
  const libraryConnection = $('library-connection');
  libraryConnection.classList.toggle('online', !!state.connected);
  const connectionLabel = state.connected ? 'Switch 2 connected' : 'Switch 2 not connected';
  if (libraryConnection.querySelector('span').textContent !== connectionLabel)
    libraryConnection.querySelector('span').textContent = connectionLabel;
  $('load').textContent=state.platform?.sdLauncher?'Install selected on SD':'Install / load selected';
  $('library-note').textContent = state.platform?.sdLauncher
    ? 'Applications are installed in SD:/IZ2S/Homebrew/Modules/. The console discovers and opens them locally. Modules requiring PC services or a separate native entry are unavailable in this mode.'
    : state.platform?.residentGraphics && state.active
    ? 'Compatible apps remain in memory for this session. '+(state.residentMemory?'Session memory: '+(state.residentMemory.usedBytes/1048576).toFixed(1)+' / '+(state.residentMemory.budgetBytes/1048576).toFixed(0)+' MiB. ':'')+'Add apps from the menu while the PC is available. Select the full set before a retained-audio session.'
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
        : 'External component is downloaded separately from the IZM package.');
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
  const problem=globalThis.IZ2S_ERRORS.describe(message),box=element('div','operation-problem');box.setAttribute('role','status');
  const title=element('p','problem-title',problem.title);title.setAttribute('translate','no');box.append(title,element('p','hint',problem.action));
  const details=element('details','technical-details'),pre=element('pre','',problem.detail);pre.setAttribute('translate','no');details.append(element('summary','','Technical details'),pre);box.append(details);container.append(box);
}
const sessionActionHomes=new Map();
function placeSessionActions(view){
 const civ=state.platform?.loaderId,selected=civ==='civswitch-ldn'?'ldn-stop':civ==='civswitch'?'civ-stop':'stop';
 const target=$('session-summary-session-actions');
 for(const id of ['stop','restore-connection','civ-stop','ldn-stop']){
  const button=$(id);if(!button||!target)continue;
  const show=view.compact&&(id===selected||id==='restore-connection'&&selected==='stop');
  if(show){if(!sessionActionHomes.has(id))sessionActionHomes.set(id,{parent:button.parentElement,next:button.nextSibling});if(button.parentElement!==target)target.append(button);}
  else {const home=sessionActionHomes.get(id);if(home?.parent&&button.parentElement!==home.parent){if(home.next?.parentElement===home.parent)home.parent.insertBefore(button,home.next);else home.parent.append(button);}}
 }
}
let configurationOpen=false,libraryBatch=false,historyLimit=10;
function renderSessionPresentation(){
 const view=globalThis.IZ2S_PRESENTATION.session(state),setup=$('setup');
 setup.classList.toggle('session-compact',view.compact);setup.classList.toggle('session-offline',view.offline||view.unconfirmed);setup.classList.toggle('session-configuration-open',configurationOpen);
 if(view.offline){$('mode').textContent='Connection interrupted';$('mode').dataset.state='offline';}
 const consoleCard=$('device-lan');if(view.offline&&consoleCard){consoleCard.classList.remove('ready','pending');consoleCard.classList.add('connection-lost');setText(consoleCard.querySelector('.state'),'CONNECTION LOST');setText(consoleCard.querySelector('.detail-text')||consoleCard.querySelector('.device-detail'),['civswitch','civswitch-ldn'].includes(state.platform?.loaderId)?'Connection lost. Stop this session, restart Civilization VII and open Load Game.':'Connection lost. Restore the connection to manage applications.');}
 if(consoleCard){consoleCard.classList.toggle('shutdown-unconfirmed',view.unconfirmed);if(view.unconfirmed){consoleCard.classList.remove('ready','pending','connection-lost');setText(consoleCard.querySelector('.state'),'Shutdown unconfirmed');setText(consoleCard.querySelector('.detail-text')||consoleCard.querySelector('.device-detail'),'Restart Civilization VII before starting a new session.');}}
 $('session-summary').hidden=!view.compact;$('session-summary-state').textContent=view.title;$('session-summary-title').textContent=view.launcher;placeSessionActions(view);
 $('session-summary-game').textContent=view.game;
 $('session-summary-copy').textContent=view.stopping?'':(view.offline?'Waiting for connection':state.fsRpc?.state==='ready'?'Filesystem access active':state.connected?'Console connected':'Connection pending');
 $('session-summary-copy').hidden=view.stopping;
 $('session-summary-note').hidden=view.stopping||!view.offline&&!!state.connected;$('session-summary-note').textContent='The console session is retained. Restore the connection to manage applications.';
 if(['civswitch','civswitch-ldn'].includes(state.platform?.loaderId)&&(view.offline||view.unconfirmed)){$('session-summary-note').hidden=false;$('session-summary-note').textContent=view.unconfirmed?'Restart Civilization VII before starting a new session.':'Connection lost. Stop this session, restart Civilization VII and open Load Game.';}
 const savedLoader=state.civswitchLdn?.saveInstallation||state.civswitch?.saveInstallation;
 if(view.running&&state.connected&&['civswitch','civswitch-ldn'].includes(state.platform?.loaderId)&&savedLoader?.state==='installed'){$('session-summary-note').hidden=false;$('session-summary-note').textContent='Save installed. Next time: Civilization VII → Load Game, then Launch in this panel.';}
 $('session-settings-label').textContent=configurationOpen?'Hide connection settings':'Connection settings';$('session-settings-toggle').setAttribute('aria-expanded',String(configurationOpen));
 if((view.running||view.offline)&&state.platform?.izModules)$('progress-note').textContent=state.connected?'Use App library to manage applications.':'The console session is retained. Restore the connection to manage applications.';
 for(const prefix of ['civ','ldn'])if(state.civLauncher?.state==='offline'){$(prefix+'-result-title').textContent='Connection interrupted';$(prefix+'-result-detail').textContent=globalThis.IZ2S_ERRORS.canonical(state.civLauncher.error||'The console session is retained.');$(prefix+'-result-detail').setAttribute('translate','no');}
 for(const prefix of ['civ','ldn'])if(state.civLauncher?.state==='running'){$(prefix+'-result-title').textContent='Launcher is running';$(prefix+'-result-detail').textContent=state.connected?'Console connected':'The console session is retained.';$(prefix+'-launch').hidden=true;}
 $('apps').classList.toggle('batch-selection',libraryBatch);$('library-batch').setAttribute('aria-pressed',String(libraryBatch));$('library-batch').textContent=libraryBatch?'Finish selection':'Select multiple';
 document.querySelector('.library-bar').hidden=!libraryBatch;
 applyLibrarySearch();
 renderDeviceTopology();
}
function applyLibrarySearch(){
  const status=$('library-status').value,location=$('library-location').value,query=$('library-search').value;
  const result=globalThis.IZ2S_PRESENTATION.libraryFilter(state,{query,status,location,focusId:libraryContext?.id});const ids=new Set(result.ids);
  for(const card of $('library').children)card.hidden=!!card.dataset.appId&&!ids.has(card.dataset.appId);
  const filtered=!!query.trim()||status!=='all'||location!=='all';$('library-reset').hidden=!filtered;
  $('library-result-count').textContent=result.ids.length+' / '+result.total;
  $('library-empty').hidden=result.ids.length>0||!state?.library?.length;
  const hidden=[...selection].filter(id=>!ids.has(id)).length;$('library-hidden-selection').hidden=!libraryBatch||!hidden;
  $('library-hidden-selection').textContent=hidden?'Selected outside this filter: '+hidden:'';
}
function resetLibraryFilters(){$('library-search').value='';$('library-status').value='all';$('library-location').value='all';applyLibrarySearch();}
$('library-reset').onclick=resetLibraryFilters;$('library-empty-reset').onclick=resetLibraryFilters;
$('library-status').onchange=()=>{if($('library-status').value==='all')$('library-location').value='all';applyLibrarySearch();};
$('library-location').onchange=()=>{if($('library-location').value!=='all'&&$('library-status').value==='all')$('library-status').value='installed';applyLibrarySearch();};
document.addEventListener('click',event=>{for(const details of document.querySelectorAll('#library .app-more[open],#library .storage-more[open]'))if(!details.contains(event.target))details.open=false;});
document.addEventListener('keydown',event=>{if(event.key==='Escape')for(const details of document.querySelectorAll('#library .app-more[open],#library .storage-more[open]')){details.open=false;details.querySelector('summary')?.focus();}});

$('session-settings-toggle').onclick=()=>{configurationOpen=!configurationOpen;renderSessionPresentation();};
$('session-summary-library').onclick=()=>switchView('apps');
$('library-search').oninput=applyLibrarySearch;
$('library-batch').onclick=()=>{libraryBatch=!libraryBatch;if(!libraryBatch){selection.clear();for(const input of document.querySelectorAll('.app-select input'))input.checked=false;}renderSessionPresentation();renderControls();};
$('history-search').oninput=()=>{historyLimit=10;historyStamp='';renderHistory();};
for(const id of ['history-filter','history-period','history-error-type'])$(id).onchange=()=>{if(id==='history-error-type'&&$(id).value!=='all')$('history-filter').value='unsuccessful';historyLimit=10;historyStamp='';renderHistory();};
for(const [id,kind]of [['history-today-all','all'],['history-today-success','successful'],['history-today-errors','unsuccessful'],['history-today-pending','pending']])$(id).onclick=()=>{$('history-period').value='today';$('history-filter').value=kind;$('history-error-type').value='all';historyLimit=10;historyStamp='';renderHistory();};
$('history-more').onclick=()=>{historyLimit+=10;historyStamp='';renderHistory();};

let historyStamp = '';
let savedLogAttempt = null;
let savedLogGeneration = 0;
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
async function downloadSessionLog(id) {
  try {
    const value = await api('/api/log?id=' + encodeURIComponent(id));
    const text = value.events.map((e) => e.at + '  ' + logMessage(e)).join('\n');
    const url = URL.createObjectURL(new Blob([text + '\n'], { type: 'text/plain;charset=utf-8' }));
    const link = element('a');
    link.href = url;
    link.download = 'IZ2S-session-' + id + '.log';
    link.click();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
  } catch (e) { notice(e.message); }
}
function invalidateSavedLog() {
  savedLogGeneration++;
  savedLogAttempt = null;
  $('download-saved-log').disabled = true;
  $('log-dialog').setAttribute('aria-busy', 'false');
}
$('log-dialog').addEventListener('close', () => {
  if (!$('log-dialog').open) invalidateSavedLog();
});
$('log-dialog').addEventListener('cancel', invalidateSavedLog);
async function openSavedLog(attempt) {
  const generation = ++savedLogGeneration,
    dialog = $('log-dialog'),
    download = $('download-saved-log'),
    created = date(attempt.created);
  savedLogAttempt = null;
  download.disabled = true;
  setText($('saved-log-title'), 'Session log' + (created ? ' · ' + created : ''));
  setText($('saved-log-attempt'),
    (attempt.kind === 'bootstrap' ? 'Bootstrap installation' : 'Launcher session') + ' · ' + attempt.id);
  setText($('saved-log'), 'Loading saved log…');
  dialog.setAttribute('aria-busy', 'true');
  if (!dialog.open) dialog.showModal();
  try {
    const value = await api('/api/log?id=' + encodeURIComponent(attempt.id));
    if (generation !== savedLogGeneration || !dialog.open) return;
    setText($('saved-log'), value.events.map((event) => event.at + '  ' + logMessage(event)).join('\n') ||
      'No further details were recorded.');
    savedLogAttempt = attempt.id;
    download.disabled = false;
  } catch (error) {
    if (generation !== savedLogGeneration || !dialog.open) return;
    setText($('saved-log'), 'Could not load this log: ' + error.message);
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
$('library-sd-refresh').onclick=()=>act('/api/sd/refresh',{});
function renderHistory() {
 const rows=historyRows(),options={query:$('history-search').value,kind:$('history-filter').value,category:$('history-error-type').value||'all',period:$('history-period').value||'today',limit:historyLimit},stamp=JSON.stringify([rows.map(row=>[row.id,row.created,row.kind,row.status,row.error,row.connectedAt]),options,historyClearing,state.active?.id,state.historyAll,new Date().toDateString()]);
 setText($('fail-count'),rows.length);$('history-scope-note').hidden=state.historyAll===true;if(stamp===historyStamp)return;historyStamp=stamp;
 const presentation=globalThis.IZ2S_PRESENTATION,today=presentation.statistics(rows);
 setText($('history-today-total'),today.total);setText($('history-today-success-count'),today.successful);setText($('history-today-error-count'),today.unsuccessful);setText($('history-today-pending-count'),today.pending);
 const typeSelect=$('history-error-type');if(typeSelect.children.length===1)for(const [key,name]of Object.entries(presentation.errorLabels)){const option=element('option','',name);option.value=key;typeSelect.append(option);}
 $('history-error-types').replaceChildren();for(const [key,count]of Object.entries(today.errors).sort((a,b)=>b[1]-a[1])){const button=element('button','quiet');button.append(element('span','',presentation.errorLabels[key]),element('strong','',String(count)));button.setAttribute('aria-pressed',String(options.category===key));button.onclick=()=>{$('history-period').value='today';$('history-filter').value='unsuccessful';$('history-error-type').value=key;historyLimit=10;historyStamp='';renderHistory();};$('history-error-types').append(button);}
 if(!today.unsuccessful)$('history-error-types').append(element('span','hint','No errors today'));
 const selected=presentation.history(rows,options);$('attempts').replaceChildren();setText($('history-count'),historyClearing?'Clearing history…':selected.rows.length+' / '+selected.total);$('history-more').hidden=!selected.more;
 $('history-clear').disabled=historyClearing||!rows.some(row=>globalThis.IZ2S_PRESENTATION.attempt(row,{activeId:state.active?.id}).deletable);
 if(!selected.rows.length)$('attempts').append(element('p','empty','No matching attempts'));
 const groups=new Map();
 for(const key of ['successful','unsuccessful','pending']){const groupRows=selected.rows.filter(row=>presentation.attempt(row).outcome===key);if(!groupRows.length)continue;const group=element('section','attempt-group');group.append(element('h2','attempt-group-title',{successful:'Successful',unsuccessful:'Unsuccessful',pending:'Not finished'}[key]));$('attempts').append(group);groups.set(key,group);}
 for(const row of selected.rows){
  const info=globalThis.IZ2S_PRESENTATION.attempt(row,{activeId:state.active?.id}),card=element('article','attempt attempt-compact'),badge=element('span','attempt-status '+info.tone,info.label),body=element('div','attempt-copy'),heading=element('div','attempt-heading');
  heading.append(element('strong','',row.kind==='bootstrap'?'First Setup':'Launcher session'),element('time','',date(row.created)));body.append(heading);
  if(row.error){const detail=element('p','attempt-detail');detail.setAttribute('translate','no');detail.textContent=globalThis.IZ2S_ERRORS.describe(row.error).title;detail.title=globalThis.IZ2S_ERRORS.canonical(row.error);body.append(detail);}
  else body.append(element('p','attempt-detail hint',info.success?'Launcher started successfully.':info.label));
  const actions=element('div','attempt-actions'),view=element('button','','View log');view.onclick=()=>openSavedLog(row);actions.append(view);
  const remove=element('button','quiet','Delete log');remove.disabled=historyClearing||!info.deletable;remove.title=info.deletable?'Delete this attempt and its full log.':'The active attempt cannot be deleted.';remove.onclick=()=>deleteAttemptLogs([row.id]);actions.append(remove);
  card.append(badge,body,actions);groups.get(info.outcome).append(card);
 }
}

function renderActivity() {
  const list = state.log.filter((e) => $('log-details').checked || e.type !== 'detail').slice(-250),
    sessionActive = !!state.active || ['starting','running','offline','stopping'].includes(state.civLauncher?.state),
    stamp = JSON.stringify([list, sessionActive]),
    log = $('log');
  $('download-session-log').disabled = !(state.active || state.lastAttempt)?.id;
  if (log.dataset.stamp === stamp) return;
  logWriting = true;
  log.dataset.stamp = stamp;
  setText($('log-count'), list.length + ' events');
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
      const message=element('span');message.setAttribute('translate','no');message.textContent=logMessage(item);
      row.append(element('time', '', logTime(item.at)),message);
    }
    placeChild(log, row, index);
  });
  if (!list.length) {
    let empty=existing.get('empty');
    if(!empty){empty=element('p','hint');empty.dataset.logKey='empty';placeChild(log,empty,0);}
    setText(empty,sessionActive?'Waiting for session messages.':'No active attempt. Device checks do not send console commands.');
  }
  requestAnimationFrame(() => {
    if (logFollow && log.clientHeight) log.scrollTop = log.scrollHeight;
    logWriting = false;
  });
}

function renderConnectionStatus() {
  const transferring=state?.active?.status==='running'&&!state?.offline&&(state.jobs||[]).some(j=>['loading','publishing'].includes(j.status))&&(!state.link||state.link.state==='connected');
  const stopping = state?.active?.status === 'stopping'||state?.civLauncher?.state==='stopping';
  const offline = state?.active?.status==='offline'||state?.civLauncher?.state==='offline';
  const unconfirmed=stopping&&(state?.active?.nativeParentRetention?.awaitingActualClose||state?.civLauncher?.shutdownUnconfirmed);
  const civNetwork=state?.platform?.loaderId==='civswitch'&&state?.civswitch?.networkReachable&&!state?.connected;
  const civUsbWaiting=state?.platform?.loaderId==='civswitch'&&state?.civswitch?.wirelessNetworkPending&&state?.civswitch?.inputStatus?.keyboard;
  const connectionMode = serverUnavailable
    ? ['disconnected', 'Server unavailable']
    : state?.simulated
      ? ['preview', 'Simulated devices']
      : state?.preview
        ? ['preview', 'Design preview']
        : unconfirmed
          ? ['disconnected','Shutdown unconfirmed']
        : stopping
          ? ['checking', 'Stopping…']
        : offline
          ? ['disconnected','Connection interrupted']
          : transferring
            ? ['transferring', 'Transferring files…']
          : state?.active && state?.link?.state === 'waiting-for-dock'
            ? ['connecting', 'Reconnecting…']
            : state?.active?.kind === 'bootstrap'
              ? ['checking', 'Setup in progress…']
              : state?.connected
                ? ['connected', 'Console connected']
                : state?.active?.launchStage === 'initializing' && state?.link?.state === 'connected'
                  ? ['checking', 'Console connected · preparing launcher']
                : civNetwork
                  ? ['checking', 'Console on LAN · CivSwitch :9026 not verified']
                : civUsbWaiting
                  ? ['checking', 'CivSwitch · waiting for LAN; USB input available']
                : state?.active
                  ? ['connecting', 'Connecting…']
                : state?.platform?.loaderId==='civswitch-ldn'
                  ? ['idle', 'Game loader not started']
                  : state?.checks?.ready
                    ? ['idle', 'Session not started']
                    : ['disconnected', 'Not connected'];
  setText($('mode'), connectionMode[1]);
  $('mode').dataset.state = connectionMode[0];
  $('connection').classList.toggle('idle', connectionMode[0] === 'idle');
  $('mode').classList.toggle('preview', !serverUnavailable && !!state?.preview);
  $('connection').classList.toggle('online', !serverUnavailable && !!state?.connected && !stopping && !offline);
  $('connection').classList.toggle('working', !serverUnavailable && ['checking','connecting'].includes(connectionMode[0]));
  const text = connectionMode[1];
  if ($('connection').lastChild.textContent !== text)
    $('connection').lastChild.textContent = text;
}
function renderLocalActionAvailability() {
  for (const control of document.querySelectorAll('.folder-button,[data-folder],.module-configure'))
    control.disabled = serverUnavailable || !!state?.preview || control.dataset.pending === 'true' || control.dataset.unavailable === 'true';
}
function renderServerAvailability() {
  workspace.setAvailable(!serverUnavailable);
  document.querySelector('.nav-dot').hidden=serverUnavailable||!state?.connected;
  renderLocalActionAvailability();
  $('panel-main').classList.toggle('server-unavailable', serverUnavailable);
  if (!serverUnavailable) return;
  civswitchUI.render(state);civswitchLdnUI.render(state);
  renderConnectionStatus();
  for (const card of document.querySelectorAll('#devices > .device')) {
    setText(card.querySelector('.state'), 'UNVERIFIED');
    setText(card.querySelector('.device-ping'), 'Server unavailable');
  }
  const libraryConnection = $('library-connection');
  libraryConnection.classList.remove('online');
  setText(libraryConnection.querySelector('span'), 'Server unavailable · device status is outdated');
  setText($('library-note'), 'Reconnect to the server to refresh device and transfer status.');
  setText($('gate-message'), 'Server unavailable · waiting for a fresh status update.');
  $('delivery-rpc-status').dataset.state = 'unavailable';
  setText($('delivery-rpc-label'), 'RPC status unavailable');
  $('console-mode-state').hidden = true;
  $('application-recovery').hidden = true;
  $('stream-link').hidden = true;
  if (state?.active) setText($('progress-title'), 'Session status unavailable');
  $('dolphin-open').disabled = true;
  setText($('dolphin-status'), 'Server unavailable · FS status cannot be checked.');
  document.querySelectorAll('#start,#load,#retry,#engine-download,#engine-rescan,#arm-install,#hardware-form button,.module-host-actions button,.module-unload,.module-install,.sd-inventory-refresh,.module-configure,.folder-button,[data-folder],[data-variant],[data-loader],.address-form button,.transport-choice button,.transport-choice input,.detected-peer,.up-start,.up-save,.up-confirm')
    .forEach(control => { control.disabled = true; });
}
function render() {
  const attempt = state.active;
  renderTransferStatus();
  $('restore-connection').hidden=state.active?.status==='stopping'||!state.link?.recoveryAvailable||state.link?.protocol!==3||state.connected&&state.active?.status!=='offline';
  $('restore-connection').disabled=serverUnavailable||state.preview||!attempt;
  workspace.updateModules(state);
  renderPlatform();
  const ended = state.lastAttempt;
  if (ended && ended.id !== finishedAttempt) {
    finishedAttempt = ended.id;
    requestId = null;
    $('screen-ready').checked = false;
    document.querySelectorAll('.up-ready,.up-source-ready').forEach(input => {input.checked = false;});
  }
  renderDevices();
  renderStartupModules();
  civswitchUI.render(state);civswitchLdnUI.render(state);
  renderLibrary();
  renderConsoleApps();
  renderHistory();
  renderActivity();
  renderWizard();
  renderManualInputDesign();
  advSetup.render({state,step,busy,serverUnavailable,manual:inputDesign==='own',preparation:cardputerRequest||state?.hardwareOperation});
  renderConnectionStatus();
  const applicationRecovery = state.applicationRecovery;
  $('application-recovery').hidden = !applicationRecovery;
  if (applicationRecovery) {
    const labels = {
      'waiting-for-client': 'Waiting for the console app to reconnect.',
      'waiting-for-network': 'Connection lost. Waiting for the app to reconnect.',
      connected: 'App state restored. Control connection is checked separately.',
      stopped: 'App recovery channel stopped.',
    };
    $('application-recovery').textContent = labels[applicationRecovery.state] || 'App recovery state is unknown.';
  }
  $('progress-box').hidden = !attempt;
  $('progress').hidden=['running','offline','complete'].includes(attempt?.status);
  $('progress-value').hidden=$('progress').hidden;
  $('progress-title').textContent = attempt
    ? {
        starting: attempt.inputActive?'Entering launch commands…':state.link?.state==='connected'||attempt.launchStage==='initializing'?'Console connected · preparing launcher':attempt.launchStage==='waiting-console'?'Input complete · waiting for console':'Preparing launcher',
        typing: 'Entering bootstrap.bas',
        running: 'Launcher is running',
        offline: 'Connection lost · launcher retained',
        stopping: 'Stopping safely…',
        complete: 'Setup complete',
        review: 'Source entered · awaiting your review',
        checking: 'Reading the source check',
        saving: 'Sending the save command',
        awaiting_restart: 'Waiting for a full console restart',
        failed: 'Attempt failed',
        cancelled: 'Input cancelled',
        stopped: 'Session stopped',
      }[attempt.status] || attempt.status
    : '';
  if (attempt?.progress) {
    $('progress').value = attempt.progress.line;
    $('progress').max = attempt.progress.total;
    $('progress-value').textContent =
      attempt.progress.line + ' / ' + attempt.progress.total + ' lines';
  } else {
    if (['complete', 'running', 'offline'].includes(attempt?.status))
      $('progress').value = 229;
    else $('progress').removeAttribute('value');
    $('progress-value').textContent = '';
  }
  $('progress-note').textContent =
    attempt?.status==='starting'&&(state.link?.state==='connected'||attempt.launchStage==='initializing')
      ? 'The console connection is established. FS RPC and graphics initialization are running; follow the session log below.'
    : attempt?.status==='starting'&&attempt.launchStage==='waiting-console'
      ? 'The input device finished its job. If SmileBASIC shows no text, check the USB keyboard connection and firmware. If the text is present, check the console screen and network.'
    : attempt?.kind === 'bootstrap'
      ? 'Source entry uses the original HID timings. Console execution and save results require your confirmation.'
      : attempt?.status === 'offline'
        ? state.platform?.residentGraphics
          ? 'The app set remains in memory until STOP. New modules require the control connection to be restored.'
          : 'A loaded local app may continue without Wi-Fi. Host commands and return to the launcher require a connection. STOP ends this retained session.'
      : state.connected
        ? 'You can close this window. Your console session will keep running.'
        : attempt?.status === 'failed'
          ? 'Restart the console before your next attempt.'
          : state.platform?.delivery?.preloaded
            ? 'RUN → prepare memory once → connect'
            : 'LOAD “TXT:' +
              (state.platform?.delivery?.bootstrapName ||
                (state.bootstrapProfile?.recovery ? 'NS2R' : 'NS2')) +
              '” → RUN → prepare memory once → connect';
  if (state.notification) {
    backendNotice = state.notification.at || state.notification.message;
    notice(state.notification.message, state.notification.type, backendNotice);
  } else if (backendNotice) {
    if (notificationKey === backendNotice) $('toast').hidden = true;
    backendNotice = '';
  }
  renderServerAvailability();
  renderMenuLayout();
  renderSessionPresentation();
}
function renderMenuLayout() {
  const id=state?.platform?.loaderId,civ=['civswitch','civswitch-ldn'].includes(id);
  $('devices').hidden=false;
  $('device-toolbar').hidden=false;
  $('connection-files').hidden=civ;
  $('mode').hidden=civ;
  $('network-caption').hidden=civ;
  for(const [button,engine]of [['civ-mode-usb','civswitch'],['civ-mode-ldn','civswitch-ldn']]){
    const needsRestart=engine==='civswitch-ldn'&&state.platform.civswitchLdnAvailable!==true;
    $(button).disabled=serverUnavailable||busy||state.platform.locked||needsRestart;
    $(button).title=needsRestart?(state.platform.locked||busy?'Restart the panel after ending the current session to enable CIV7 LAN.':'Restart the panel to enable CIV7 LAN.'):'';
  }
  document.querySelector('[data-view="apps"]').hidden=!state?.platform?.izModules&&!civ;
  $('hardware-options').hidden=id==='civswitch-ldn';
  if(id==='civswitch-ldn')$('adv-usb-next').hidden=true;
  $('civ-input-settings').hidden=true;
  if(civ){$('iz-flow').hidden=true;$('installation-options').hidden=true;}
}

let platformMenuStamp = '',
  configurationStamp = '';
const upstreamSteps = { izmysh: 'launch' };
function fieldsFor(schema) {
  if (Array.isArray(schema)) return schema;
  if (schema?.fields) return schema.fields;
  return Object.entries(schema?.properties || schema || {}).map(([id, v]) => ({
    id,
    ...v,
  }));
}
function buildFields(container, schema, values) {
  container.replaceChildren();
  for (const field of fieldsFor(schema)) {
    const id = field.id || field.key || field.name;
    if (!id) continue;
    const label = element('label', 'configuration-field'),
      input = field.options ? element('select') : element('input');
    input.name = id;
    input.dataset.kind = field.type || 'string';
    if (field.options)
      for (const value of field.options) {
        const o = element(
          'option',
          '',
          typeof value === 'object' ? value.label : value,
        );
        o.value = typeof value === 'object' ? value.value : value;
        input.append(o);
      }
    else
      input.type = ['boolean', 'checkbox'].includes(field.type)
        ? 'checkbox'
        : field.type === 'number'
          ? 'number'
          : field.type === 'password' ? 'password' : 'text';
    if (input.type === 'checkbox') {
      input.classList.add('panel-switch');
      input.setAttribute('role', 'switch');
    }
    input.autocomplete = 'off';
    const value = values?.[id] ?? field.default ?? '';
    if (input.type === 'checkbox') input.checked = !!value;
    else input.value = value;
    label.append(element('span', '', field.label || field.title || id), input);
    if (field.description || field.help)
      label.append(element('small', 'hint', field.description || field.help));
    container.append(label);
  }
}
function fieldValues(container) {
  const value = {};
  for (const input of container.querySelectorAll('input,select'))
    value[input.name] =
      input.type === 'checkbox'
        ? input.checked
        : input.dataset.kind === 'number'
          ? Number(input.value)
          : input.value.trim();
  return value;
}
function upstreamPanel(id) {
  return document.querySelector('[data-upstream-panel="' + id + '"]');
}
function renderAutomaticRpcStatus(meta) {
  const loader = meta.loaders.find(item => item.id === meta.loaderId);
  const status = $('delivery-rpc-status'),
    automatic = meta.izModules && loader?.settingsSchema?.some(item => item.key === 'rpcMode'),
    dirty = deliveryDirty(),
    fs = state.active ? state.fsRpc : null;
  status.hidden = !automatic;
  if (!automatic) {
    setText($('delivery-rpc-note'), dirty ? 'Saving session settings…' : 'Applies to First Setup and Launch.');
    return;
  }
  status.dataset.state = fs?.state || 'automatic';
  setText($('delivery-rpc-label'), fs?.state === 'ready'
    ? 'Filesystem access · Active'
    : fs?.state === 'starting' ? 'Filesystem access · Connecting'
    : fs?.state === 'failed' ? 'Filesystem access · Unavailable' : meta.loaderSettings?.fsRpcMode ? 'Enabled for next launch' : 'Disabled');
  const note = dirty ? 'Saving session settings…'
    : fs?.state === 'ready' ? 'Filesystem access is active for this session.'
    : fs?.state === 'starting' ? 'Connecting to the console filesystem…'
    : fs?.state === 'failed' ? globalThis.IZ2S_ERRORS.canonical(fs.error) || 'Filesystem access is unavailable for this session.'
    : state.active ? 'Session settings are locked until this session ends.'
    : meta.loaderSettings?.fsRpcMode ? 'Filesystem access will be requested before the launcher starts.'
    : 'Enable filesystem access before starting a session.';
  setText($('delivery-rpc-note'), note);
}
function renderPlatform() {
  const meta = state?.platform;
  if (!meta) return;
  const civ=['civswitch','civswitch-ldn'].includes(meta.loaderId);
  $('civ-mode-selector').hidden=!civ;
  for(const [id,engine] of [['civ-mode-usb','civswitch'],['civ-mode-ldn','civswitch-ldn']]){
    $(id).classList.toggle('selected',meta.loaderId===engine);
    $(id).setAttribute('aria-pressed',String(meta.loaderId===engine));
  }
  for(const [id,selectedGame]of [['game-sm4',!civ],['game-civ7',civ]]){
    $(id).classList.toggle('selected',selectedGame);$(id).setAttribute('aria-pressed',String(selectedGame));$(id).disabled=busy||meta.locked;
  }
  document.querySelector('.launch-selector').hidden=false;
  $('engine-options').hidden=civ;
  const shellId = civ?(state.civLauncher?.shell||'izlauncher'):meta.izModules?meta.variant||'izlauncher':null;
  const shellName = meta.shells?.find(item => item.id === shellId)?.name || (meta.izModules ? 'IZlauncher' : 'Core only');
  for (const button of document.querySelectorAll('[data-folder="shell"]')) {
    button.hidden = !shellId;
    button.querySelector('span').textContent = shellName + ' folder';
    button.setAttribute('aria-label', shellName + ' folder');
    button.title = 'Open the selected shell folder';
  }
  $('footer-shell').textContent = shellName;
  const locked = busy || meta.locked,
    selected = meta.loaderId;
  const engineChoices = meta.loaders.filter(loader => !['civswitch','civswitch-ldn'].includes(loader.id)&&(meta.variant === 'original' || RUN21_CATALOG.engineAvailable(loader, meta.variant)));
  const inventoryStamp = JSON.stringify(
    engineChoices.map((l) => [l.id, l.name, l.integrationStatus]),
  );
  const tabs = document.querySelector('.loader-tabs');
  tabs.hidden = !engineChoices.length;
  tabs.style.setProperty('--engine-count', engineChoices.length);
  if (tabs.dataset.inventory !== inventoryStamp) {
    tabs.dataset.inventory = inventoryStamp;
    tabs.replaceChildren();
    engineChoices.forEach((loader, i) => {
      const b = element('button');
      b.type = 'button';
      b.dataset.loader = loader.id;
      b.setAttribute('role', 'tab');
      b.append(
        element('span', '', String(i + 1).padStart(2, '0')),
        element('strong', '', loader.name),
      );
      b.append(element('small', 'engine-integration-status', loader.integrationStatus || ''));
      b.onclick = () => act('/api/launch-settings', { loaderId: loader.id });
      tabs.append(b);
    });
  }
  const shellSelect = $('launch-runtime'),
    shellStamp = JSON.stringify((meta.shells || []).map((p) => [p.id, p.name]));
  if (shellSelect.dataset.inventory !== shellStamp) {
    shellSelect.dataset.inventory = shellStamp;
    shellSelect.replaceChildren();
    for (const v of [
      ...(meta.shells || []),
      { id: 'original', name: 'Core only' },
    ]) {
      const b = element('button', '', v.name);
      b.type = 'button';
      b.dataset.variant = v.id;
      b.onclick = () => ['civswitch','civswitch-ldn'].includes(state.platform.loaderId)?act('/api/'+state.platform.loaderId,{action:'select-shell',launcher:v.id}):act('/api/launch-settings', { variant: v.id });
      shellSelect.append(b);
    }
  }
  document.querySelector('[data-step=bootstrap]').hidden =
    meta.loaders.find((l) => l.id === selected)?.menu?.sourceSetup === false;
  document.querySelector('[data-view=apps]').disabled = !meta.izModules&&!civ;
  document.querySelector('[data-view=apps]').title = meta.izModules||civ
    ? ''
    : 'Select IZlauncher or IZPSlauncher to use IZM modules';
  if (!meta.izModules && !civ && !$('apps').hidden) switchView('setup');
  $('iz-flow').hidden = !meta.izModules||['civswitch','civswitch-ldn'].includes(meta.loaderId);
  const label = meta.loaders.find((l) => l.id === selected)?.name || selected;
  setText($('engine-folder-name'), label);
  setText($('engine-folder-path'), meta.loaders.find(loader => loader.id === selected)?.folder || 'Folder path is unavailable. Use the Engine button above.');
  const errors = $('extension-errors');
  if (errors) {
    errors.hidden = !meta.extensionErrors?.length;
    errors.setAttribute('translate','no');errors.textContent = (meta.extensionErrors || [])
      .map((e) => e.id + ': ' + globalThis.IZ2S_ERRORS.canonical(e.error))
      .join(' · ');
  }
  $('delivery-summary').textContent = label;
  document.querySelector('[data-variant=original]').textContent = 'Core only';
  const deliverySchema = (
    meta.loaders.find((l) => l.id === selected)?.settingsSchema || []
  ).filter(
    (f) =>
      (!f.runtimes || f.runtimes.includes('izlauncher')) &&
      f.key !== 'holdRpc' &&
      f.key !== 'mode' &&
      f.key !== 'basicArity',
  );
  const deliveryForm = $('delivery-options'),
    deliverySchemaStamp = JSON.stringify([selected, deliverySchema]),
    deliveryStamp = JSON.stringify(meta.loaderSettings);
  if (deliveryForm.dataset.schema !== deliverySchemaStamp) {
    buildFields($('delivery-fields'), deliverySchema.map(field => field.key === 'fsRpcMode'
      ? {...field, label: 'Filesystem access', description: '', help: ''} : field), meta.loaderSettings);
    const fsInput = $('delivery-fields').querySelector('[name=fsRpcMode]');
    if (fsInput) {
      fsInput.setAttribute('aria-describedby', 'delivery-rpc-note delivery-fs-help');
    }
    deliveryForm.dataset.schema = deliverySchemaStamp;
    delete deliveryForm.dataset.stamp;
  }
  $('delivery-fs-help').hidden = !deliverySchema.some(field => field.key === 'fsRpcMode');
  if (deliveryForm.dataset.stamp !== deliveryStamp) {
    for (const input of $('delivery-fields').querySelectorAll('input,select')) {
      const field = deliverySchema.find(item => (item.id || item.key || item.name) === input.name),
        saved = meta.loaderSettings?.[input.name] ?? field?.default ?? '',
        value = input.type === 'checkbox' ? !!saved : saved,
        draft = input.type === 'checkbox' ? input.checked : input.value;
      // Preserve an unsaved edit when another panel updates the saved settings.
      if (input.dataset.savedValue === undefined || String(draft) === input.dataset.savedValue) {
        if (input.type === 'checkbox') input.checked = value;
        else input.value = value;
      }
      input.dataset.savedValue = String(value);
    }
    deliveryForm.dataset.stamp = deliveryStamp;
  }
  $('delivery-options').hidden = !meta.izModules || !deliverySchema.length;
  $('delivery-options')
    .querySelectorAll('input,select,button')
    .forEach((e) => (e.disabled = locked));
  $('launch-runtime').hidden = false;
  document.querySelectorAll('[data-loader]').forEach((button) => {
    button.classList.toggle('selected', button.dataset.loader === selected);
    const entry = meta.loaders.find(loader => loader.id === button.dataset.loader);
    const available = RUN21_CATALOG.engineAvailable(entry, meta.variant);
    const integrationStatus = button.querySelector('.engine-integration-status');
    if (integrationStatus) {
      const installing = button.dataset.loader === selected && state.active?.kind === 'bootstrap';
      integrationStatus.textContent = installing && state.active.progress
        ? 'Slot ' + (state.active.setup?.sourceSlot ?? 0) + ' · ' + state.active.progress.line + '/' + state.active.progress.total
        : available ? entry?.integrationStatus || '' : 'Core only mode';
    }
    button.setAttribute(
      'aria-selected',
      String(button.dataset.loader === selected),
    );
    button.disabled = locked || !available;
    button.classList.toggle('unavailable', !available);
    button.setAttribute('aria-disabled', String(locked || !available));
    button.title = available ? '' : 'Available in Core only mode';
  });
  document.querySelectorAll('[data-variant]').forEach((button) => {
    button.classList.toggle(
      'selected',
      button.dataset.variant === (civ?state.civLauncher?.shell||'izlauncher':meta.variant),
    );
    button.disabled = locked;
  });
  const configuration = meta.configurations.find(
      (c) => c.id === meta.configurationId,
    ),
    stamp = JSON.stringify([
      meta.configurationId,
      configuration?.settingsSchema,
      configuration?.fields,
      meta.profiles?.[meta.configurationId],
    ]);
  if (
    stamp !== configurationStamp &&
    !$('hardware-form').contains(document.activeElement)
  ) {
    configurationStamp = stamp;
    buildFields(
      $('hardware-fields'),
      configuration?.settingsSchema || configuration?.fields || [],
      meta.profiles?.[meta.configurationId] || {},
    );
  }
  $('hardware-form')
    .querySelectorAll('input,select,button')
    .forEach((e) => (e.disabled = locked));
  renderArmSetup(meta);
  const needs = meta.configuration.setupRequired;
  $('configuration-note').textContent =
    (needs ? 'Setup required. ' : '') +
    (Array.isArray(meta.configuration.requirements)
      ? meta.configuration.requirements
          .map((x) => (typeof x === 'string' ? x : x.message || x.label || ''))
          .filter(Boolean)
          .join(' · ')
      : meta.configuration.requirements || '');
  const menuStamp = JSON.stringify(
    meta.loaders.map((l) => [l.id, l.menu, l.settingsSchema]),
  );
  if (menuStamp !== platformMenuStamp) {
    platformMenuStamp = menuStamp;
    $('upstream-menus').replaceChildren();
    for (const loader of meta.loaders.filter(loader => !['civswitch','civswitch-ldn'].includes(loader.id))) {
      const id = loader.id;
      upstreamSteps[id] ??= 'launch';
      const panel = element('section', 'installation upstream-menu');
      panel.dataset.upstreamPanel = id;
      const heading = element('div', 'section-heading'),
        title = element('div'),
        tabs = element('div', 'segmented');
      title.append(
        element('span', 'eyebrow', 'CONNECTION CORE'),
        element('h2', '', loader.menu?.title || loader.name || id),
      );
      for (const value of loader.menu?.sourceSetup === false
        ? ['launch']
        : ['bootstrap', 'launch']) {
        const button = element(
          'button',
          '',
          value === 'launch' ? 'Launch' : 'First setup',
        );
        button.type = 'button';
        button.dataset.upstreamStep = value;
        button.onclick = () => {
          upstreamSteps[id] = value;
          panel.querySelector('.up-ready').checked = false;
          renderPlatformControls();
        };
        tabs.append(button);
      }
      heading.append(title, tabs);
      panel.append(
        heading,
        element(
          'p',
          'subtitle',
          loader.menu?.description ||
            loader.menu?.subtitle ||
            loader.description ||
            '',
        ),
      );
      const fields = element('form', 'configuration-form loader-settings'),
        fieldBox = element('div');
      fieldBox.className = 'loader-fields';
      fields.append(fieldBox);
      fields.oninput = () => {
        panel.querySelector('.up-ready').checked = false;
        renderPlatformControls();
      };
      const saveSettings = async () => {
        const meta = state?.platform;
        if (busy || meta?.locked || meta?.loaderId !== id || !fieldsDirty(fieldBox, meta.loaderSettings)) return;
        const loaderSettings = {...meta.loaderSettings, ...fieldValues(fieldBox)};
        const result = await act('/api/launch-settings', {loaderSettings});
        if (state?.platform?.loaderId !== id) return;
        if (result?.ok) state.platform.loaderSettings = loaderSettings;
        // Restore confirmed settings after an error; a draft is never presented
        // as the settings that the next launch will actually use.
        buildFields(fieldBox, (loader.settingsSchema || []).filter(field => !field.runtimes || field.runtimes.includes('original')), state.platform.loaderSettings);
        delete panel.dataset.fieldsStamp;
        renderPlatformControls();
      };
      fields.onchange = saveSettings;
      fields.onsubmit = event => {event.preventDefault(); void saveSettings();};
      panel.append(fields);
      const note = element('p', 'subtitle up-description'),
        source = element('button', 'quiet up-source', 'Download source .bas');
      source.type = 'button';
      source.onclick = async () => {
        try {
          const response = await fetch(
            '/api/loader-source?id=' + encodeURIComponent(id),
            {
              headers: { 'x-iz-key': key },
            },
          );
          if (!response.ok) throw Error((await response.json()).error);
          const url = URL.createObjectURL(await response.blob()),
            link = element('a');
          link.href = url;
          link.download = id + '-bootstrap.bas';
          link.click();
          setTimeout(() => URL.revokeObjectURL(url), 1000);
        } catch (e) {
          notice(e.message);
        }
      };
      const ack = element('label', 'ack'),
        ready = element('input'),
        readyText = element(
          'span',
          '',
          'Switch is fully restarted and SmileBASIC Direct is open.',
        );
      ready.type = 'checkbox';
      ready.className = 'up-ready';
      readyText.className = 'up-ready-label';
      ready.onchange = renderPlatformControls;
      ack.append(ready, readyText);
      const actions = element('div', 'launch-row'),
        start = element('button', 'primary up-start', 'Launch'),
        saveSource = element('button', 'secondary up-save', 'Save source'),
        confirm = element(
          'button',
          'secondary up-confirm',
          'Source saved · continue',
        ),
        stop = element('button', 'soft-stop up-stop danger', 'Stop session'),
        hard = element('button', 'hard-stop up-hard', '! STOP'),
        status = element('span', 'hint up-status');
      start.onclick = () =>
        act('/api/start', {
          kind: upstreamSteps[id] || 'launch',
          requestId: crypto.randomUUID(),
          screenReady: ready.checked,
          sourceReady: panel.querySelector('.up-source-ready').checked,
          moduleIds: [],
        });
      saveSource.onclick = () =>
        act('/api/setup', { action: 'save', requestId: crypto.randomUUID() });
      confirm.onclick = () =>
        act('/api/setup', {
          action: 'saved',
          requestId: crypto.randomUUID(),
          savedConfirmed: true,
        });
      stop.onclick = () => act('/api/stop', {});
      hard.onclick = async () => {
        if (hardStopping) return;
        hardStopping = true;
        try {
          await api('/api/hard-stop', {});
          await poll();
        } catch (e) {
          notice(e.message);
        } finally {
          hardStopping = false;
          renderPlatformControls();
        }
      };
      actions.append(start, saveSource, confirm, stop, hard, status);
      const sourceAck = element('label', 'ack source-preparation'),
        sourceReady = element('input'),
        sourceText = element('span');
      sourceReady.type = 'checkbox';
      sourceReady.className = 'up-source-ready';
      sourceReady.onchange = renderPlatformControls;
      sourceText.className = 'up-source-label';
      sourceAck.append(sourceReady, sourceText);
      panel.append(note, source, sourceAck, ack, actions);
      $('upstream-menus').append(panel);
    }
  }
  for (const loader of meta.loaders) {
    const panel = upstreamPanel(loader.id);
    if (!panel) continue;
    panel.hidden = meta.izModules || loader.id !== selected;
    const fieldBox = panel.querySelector('.loader-fields'),
      fieldsStamp = JSON.stringify([
        loader.id,
        loader.settingsSchema,
        selected === loader.id ? meta.loaderSettings : {},
      ]);
    if (
      panel.dataset.fieldsStamp !== fieldsStamp &&
      !panel.querySelector('.loader-settings').contains(document.activeElement)
    ) {
      panel.dataset.fieldsStamp = fieldsStamp;
      buildFields(
        fieldBox,
        (loader.settingsSchema || []).filter(f => !f.runtimes || f.runtimes.includes('original')),
        selected === loader.id ? meta.loaderSettings : {},
      );
    }
    panel.querySelector('.loader-settings').hidden = !fieldBox.children.length;
  }
  const inputName =
    meta.configurationId === 'arm-only'
      ? 'ARM PC · local input'
      : meta.configurationId === 'pc-cardputer-adv'
        ? 'Cardputer ADV'
        : 'ARM PC';
  words.arm[0] = inputName;
  words.usb[0] = inputName === 'ARM PC' ? 'ARM Type C - Switch' : 'USB · ' + inputName + ' → Switch';
  for (const row of state.checks.items) {
    const card = $('device-' + row.id);
    if (card && words[row.id])
      setText(card.querySelector('h3'), row.label || words[row.id][0]);
  }
  renderPlatformControls();
  renderEngineInstall();
}
function renderInstallationSlot() {
  if(state?.platform?.configuration?.externalWorkflow){$('installation-options').hidden=true;return;}
  const meta = state?.platform,
    installation = meta?.installation;
  const firstSetup = meta?.izModules
    ? step === 'bootstrap'
    : upstreamSteps[meta?.loaderId] === 'bootstrap';
  $('installation-options').hidden = !installation || !firstSetup;
  if (!installation) return;
  $('install-slot').value = String(installation.slot);
  $('install-slot').disabled = busy || !!meta.locked;
  $('install-slot-commands').textContent =
    installation.list + ' → source entry → ' + installation.save;
  $('install-slot-warning').querySelector('span').textContent = 'Installation can overwrite code in slot ' + installation.slot + ' and the saved file TXT:' + installation.name + '. Back up anything you need first.';
}
function renderEngineInstall() {
  const meta = state?.platform;
  if (!meta) return;
  if(meta.configuration.externalWorkflow){$('engine-install').hidden=true;return;}
  const missing = meta.engineInstalled === false;
  $('engine-install').hidden = !missing;
  const selectedEngine = meta.loaders.find((e) => e.id === meta.loaderId);
  $('engine-source').href = selectedEngine?.upstream?.repository || '#';
  $('engine-install-note').textContent =
    'Download the engine source from its published repository, or add it manually. The engine runs only when you launch a session.';
  $('engine-install-folder').textContent =
    'Manual installation: copy the complete repository into engines/' +
    (selectedEngine?.directory || meta.loaderId) + '/upstream/, then choose Check manual folder.';
  $('engine-install-folder').title = meta.engineInstallation?.folder || '';
  $('engine-download').disabled = serverUnavailable || busy || meta.locked || state.preview;
  $('engine-rescan').disabled = serverUnavailable || busy || meta.locked;
  if (missing) {
    $('iz-flow').hidden = true;
    $('installation-options').hidden = true;
    for (const p of document.querySelectorAll('[data-upstream-panel]'))
      p.hidden = true;
  }
}
function renderPlatformControls() {
  renderInstallationSlot();
  const meta = state?.platform;
  if (!meta) return;
  const locked = serverUnavailable || busy || meta.locked;
  $('game-sm4').disabled=locked;$('game-civ7').disabled=locked;
  $('library-source-options').hidden=meta.variant==='original'||meta.loaderId==='civswitch';
  $('library-source').value=meta.sdLauncher?'sd':'pc';$('library-source').disabled=locked;
  $('library-source-note').textContent=meta.sdLauncher?'Install compatible apps to console SD in App library first. This source requires filesystem access.':'Load apps from the PC into RAM using Startup modules or App library. They are cleared when the game closes.';
  $('library-source-lock').textContent=state.active||meta.locked?'Stop the session to change the application source.':'Choose the source before starting the launcher.';
  const sdSource=$('library-source').querySelector('option[value=sd]');if(sdSource){sdSource.disabled=meta.loaderId!=='izmysh';sdSource.title=sdSource.disabled?'SD mode currently requires IZ2S Core':'';}

  $('native-recovery-option').hidden=meta.variant==='original';
  $('native-recovery-enabled').checked=meta.nativeRecovery===true;
  $('native-recovery-enabled').disabled=true;
  document.querySelectorAll('[data-variant]').forEach(button => {
    button.disabled = locked;
    button.setAttribute('aria-pressed', String(button.classList.contains('selected')));
  });
  document.querySelectorAll('[data-loader]').forEach(button => {
    const available = RUN21_CATALOG.engineAvailable(meta.loaders.find(loader => loader.id === button.dataset.loader), meta.variant);
    button.disabled = locked || !available;
    button.setAttribute('aria-disabled', String(locked || !available));
  });
  document.querySelectorAll('#delivery-options input,#delivery-options select,#delivery-options button,#hardware-form input,#hardware-form select,#hardware-form button')
    .forEach(control => { control.disabled = locked; });
  renderAutomaticRpcStatus(meta);
  for (const panel of document.querySelectorAll('[data-upstream-panel]')) {
    const id = panel.dataset.upstreamPanel,
      phase = upstreamSteps[id] || 'launch',
      preparation = state.sourcePreparation,
      active = state.active,
      locked = busy || meta.locked,
      stage = active?.setup?.stage;
    const fields = panel.querySelector('.loader-fields'),
      draft = fieldValues(fields),
      dirty = id === meta.loaderId && fieldsDirty(fields, meta.loaderSettings),
      preloaded = draft.bootstrapMode === 'loaded';
    const sourceRequired =
      phase === 'launch' && !preloaded && !preparation?.ready;
    panel.querySelectorAll('[data-upstream-step]').forEach((button) => {
      button.classList.toggle(
        'selected',
        button.dataset.upstreamStep === phase,
      );
      button.disabled = locked;
    });
    panel
      .querySelectorAll(
        '.loader-settings input,.loader-settings select,.loader-settings button',
      )
      .forEach((e) => (e.disabled = locked));
    panel.querySelector('.source-preparation').hidden = !sourceRequired;
    panel.querySelector('.up-source-label').textContent =
      'I confirm: the selected bootstrap has been saved as ' +
      (draft.bootstrapName || preparation?.name || 'NS2') +
      '.';
    panel.querySelector('.up-source-ready').disabled = locked;
    panel.querySelector('.up-source').hidden = !preparation?.name;
    panel.querySelector('.up-ready-label').textContent =
      phase === 'bootstrap'
        ? 'Fresh SmileBASIC Direct is open; slot ' +
          (preparation?.sourceSlot ?? 0) +
          ' is empty; English (US) keyboard, Caps Lock off.'
        : preloaded
          ? 'After a full restart, the bootstrap is loaded in slot 0 and Direct is open; RUN and REMOVE have not been executed.'
          : 'After a full restart, SmileBASIC Direct is open; RUN and REMOVE have not been executed.';
    if (meta.configuration?.inputAdapter === 'cardputer-wifi' &&
        meta.profiles?.[meta.configurationId]?.cardputerCapsLock === 'on') {
      const label = panel.querySelector('.up-ready-label');
      label.textContent = label.textContent.replace('Caps Lock off.', 'Caps Lock on.');
    }
    panel.querySelector('.up-description').textContent =
      phase === 'launch'
        ? preloaded
          ? 'Source is already in slot 0. The engine will provide the connection commands.'
          : 'The panel will load the saved file ' +
            (draft.bootstrapName || preparation?.name || 'bootstrap') +
            ' with LOAD confirmation, then enter RUN → REMOVE → START.'
        : 'Source will be entered into empty slot ' +
          (preparation?.sourceSlot ?? 0) +
          '. Save it separately as ' +
          (preparation?.name || 'NS2') +
          ', then restart Switch. Other slots remain unchanged.';
    const start = panel.querySelector('.up-start');
    start.textContent =
      phase === 'launch' ? 'Launch' : 'Enter source';
    start.hidden = !!active || pendingStart || !!stopTask;
    start.disabled =
      busy ||
      dirty ||
      !!state.preview ||
      !RUN21_CATALOG.engineAvailable(meta.loaders.find(loader => loader.id === id), meta.variant) ||
      !state.checks.ready ||
      !panel.querySelector('.up-ready').checked ||
      (sourceRequired && !panel.querySelector('.up-source-ready').checked) ||
      !!state.emergencyInputPending;
    panel.querySelector('.up-ready').disabled = locked;
    panel.querySelector('.up-save').hidden = stage !== 'review';
    panel.querySelector('.up-save').disabled = busy;
    panel.querySelector('.up-confirm').hidden = stage !== 'save_confirmation';
    panel.querySelector('.up-confirm').disabled = busy;
    panel.querySelector('.up-stop').hidden = !active && !pendingStart && !stopTask;
    panel.querySelector('.up-stop').disabled =
      !!stopTask || active?.status === 'stopping';
    panel.querySelector('.up-hard').hidden =
      !active && !state.emergencyInputPending;
    panel.querySelector('.up-hard').disabled = hardStopping;
    panel.querySelector('.up-status').textContent = active
      ? active.inputActive
        ? 'Entering commands…'
        : stage === 'review'
          ? 'Review the source on Switch, then save.'
          : stage === 'save_confirmation'
            ? 'Confirm saving on Switch before continuing.'
            : active.status
      : state.emergencyInputPending
        ? 'Input cancellation needs confirmation.'
        : dirty
          ? 'Saving session settings…'
          : state.preview
            ? 'Preview · device commands disabled'
            : !state.checks.ready
              ? 'Configure the connections to continue.'
              : !panel.querySelector('.up-ready').checked
                ? 'Confirm the console screen to continue'
                : sourceRequired && !panel.querySelector('.up-source-ready').checked
                  ? 'Confirm the saved SmileBASIC program to continue.'
                  : 'Ready to launch.';
  }
  renderServerAvailability();
}
document
  .querySelectorAll('[data-loader]')
  .forEach(
    (button) =>
      (button.onclick = () =>
        act('/api/launch-settings', { loaderId: button.dataset.loader })),
  );
$('game-civ7').onclick=()=>act('/api/launch-settings',{loaderId:state?.platform?.civswitchLdnAvailable===true?'civswitch-ldn':'civswitch',variant:'original'});
$('civ-mode-usb').onclick=()=>act('/api/launch-settings',{loaderId:'civswitch',variant:'original'});
$('civ-mode-ldn').onclick=()=>act('/api/launch-settings',{loaderId:'civswitch-ldn',variant:'original'});
$('game-sm4').onclick=()=>act('/api/launch-settings',{loaderId:['civswitch','civswitch-ldn'].includes(state?.platform?.loaderId)?'izmysh':state.platform.loaderId,variant:state?.platform?.variant==='original'?'izlauncher':state.platform.variant});
document
  .querySelectorAll('[data-variant]')
  .forEach(
    (button) =>
      (button.onclick = () =>
        act('/api/launch-settings', { variant: button.dataset.variant })),
  );
$('hardware-form').onsubmit = (e) => {
  e.preventDefault();
  const profile=fieldValues($('hardware-fields'));
  if(profile.cardputerEndpoint){
    const address=profile.cardputerEndpoint.trim();
    profile.cardputerEndpoint=address.includes('://')?address:'http://'+address;
  }
  act('/api/launch-settings', {
    configurationId: state.platform.configurationId,
    profile,
  });
};

let polling = null;
let initialRenderComplete = false;
let dolphinOpening = false;
let dolphinMessage = '';
function dolphinReady() {
  return !serverUnavailable && !!state?.connected && state?.active?.status === 'running' && state?.fsRpc?.state === 'ready' && !state?.preview;
}
function renderDolphin() {
  const button = $('dolphin-open'), status = $('dolphin-status'), entry = $('console-files-entry');
  if (!button || !status) return;
  const ready = dolphinReady(), connecting=state?.fsRpc?.state==='starting';
  const tone=serverUnavailable?'offline':dolphinOpening?'opening':ready?'ready':connecting?'waiting':'unavailable';
  button.disabled = !ready || dolphinOpening;
  button.title=ready?'Open console files in Super File Bros.':'Requires an active session with Filesystem access';
  button.setAttribute('aria-busy',String(dolphinOpening));
  if(entry){
    entry.hidden=serverUnavailable||!state?.connected;
    entry.dataset.state=tone;
  }
  setText(status, serverUnavailable ? 'Server unavailable · FS status cannot be checked.' : dolphinOpening ? 'Opening file manager…' : ready
    ? dolphinMessage || 'Ready to open'
    : connecting ? 'Connecting to console files…'
    : state?.active?.status==='running'&&!state?.connected ? 'Restore the console connection to open files.'
    : 'Start a session with Filesystem access to open files.');
}
async function openDolphin(automatic = false) {
  if (!dolphinReady() || dolphinOpening) return;
  if (!workspaceReady) {if(!automatic)notice('Reopen IZ2S to enable module tabs.');return;}
  const attemptId = state.active.id;
  dolphinOpening = true;
  renderDolphin();
  try {
    const request = {attemptId, automatic};
    let result;
    try { result = await api('/api/rpc/open', request); }
    catch (error) {
      // The session owner can predate this UI update; its folder route loads lazily.
      if (!/Unknown action|Not found/.test(error.message)) throw error;
      result = await api('/api/folder/open', {kind: 'rpc-browser', ...request});
    }
    if(result.tab)workspace.openRequest(result.tab,!automatic);
    dolphinMessage = result.skipped ? '' : 'Super File Bros. tab is open.';
  } catch (error) {
    dolphinMessage = error.message;
    notice('Super File Bros.: ' + error.message);
  } finally {
    dolphinOpening = false;
    renderDolphin();
  }
}
if ($('dolphin-open')) $('dolphin-open').onclick = () => openDolphin();
let fsRpcLogProof = {id: null, state: null, checked: 0};
async function readFsRpcProof(snapshot) {
  // A panel already open during an update can keep serving its previous API.
  // Read that session's persisted proof instead of restarting a live FS owner.
  if (snapshot.fsRpc !== undefined || snapshot.platform?.loaderId !== 'bermthesequel') return;
  const id = snapshot.active?.id;
  if (!id) return;
  if (fsRpcLogProof.id !== id) fsRpcLogProof = {id, state: null, checked: 0};
  if (fsRpcLogProof.state !== 'ready' && Date.now() - fsRpcLogProof.checked > 10000) {
    fsRpcLogProof.checked = Date.now();
    try {
      const result = await api('/api/log?id=' + encodeURIComponent(id), undefined, {timeoutMs: 15000});
      const messages = (result.events || []).map(event => event.message || '');
      if (messages.some(message => message === 'FS RPC ready (--rpc): native FS execution verified; endpoint retained.'))
        fsRpcLogProof.state = 'ready';
      else if (messages.includes('Preparing held FS RPC before graphics.')) fsRpcLogProof.state = 'starting';
    } catch { /* The ordinary connection state still controls availability. */ }
  }
  if (fsRpcLogProof.state) snapshot.fsRpc = {state: fsRpcLogProof.state, source: 'session-proof-log'};
}
function poll() {
  if (polling) return polling;
  if (!initialRenderComplete) {
    $('startup-retry').disabled = true;
    setText($('startup-message'), 'Preparing the app library. The panel will open automatically.');
  }
  const generation = actionGeneration;
  polling = (async () => {
    let readingState = true;
    try {
      const snapshot = await api(initialRenderComplete ? '/api/state' : '/api/startup', undefined,
        {timeoutMs: initialRenderComplete ? 15000 : 120000});
      if (generation !== actionGeneration) return;
      readingState = false;
      state = RUN21_CATALOG.normalizeSnapshot(snapshot);
      if(['civswitch','civswitch-ldn'].includes(state.platform?.loaderId)){
        workspaceReady=false;workspace.sync([]);
      }else{
        await readFsRpcProof(state);
        try {const tools=await api('/api/workspace',undefined,{timeoutMs:5000});workspaceReady=true;workspace.sync(tools.tabs||[]);} catch {workspaceReady=false;}
      }
      if (generation !== actionGeneration) return;
      if (serverUnavailable && notificationKey === 'server-unavailable') $('toast').hidden = true;
      serverUnavailable = false;
      render();
      renderDolphin();
      if (!initialRenderComplete) {
        initialRenderComplete = true;
        $('panel-sidebar').hidden = false;
        $('panel-main').hidden = false;
        $('panel-startup').hidden = true;
      }

    } catch (e) {
      if (generation !== actionGeneration) return;
      if (!readingState) {
        console.error('IZ2S panel rendering failed', e);
        const message = 'The panel could not display the server response. Try again or reopen IZ2S.';
        if (!initialRenderComplete) {
          setText($('startup-message'), message);
          $('startup-retry').hidden = false;
          $('startup-retry').disabled = false;
        } else notice(message, 'error', 'panel-render-failed');
        return;
      }
      if (!initialRenderComplete && (e.name === 'AbortError' || e.statusCode === 503)) {
        serverUnavailable = false;
        setText($('startup-message'), 'Preparation is taking longer than usual. The panel will retry automatically.');
        $('startup-retry').hidden = false;
        $('startup-retry').disabled = false;
        return;
      }
      if (!initialRenderComplete && e.statusCode === 400) {
        serverUnavailable = false;
        setText($('startup-message'), 'The app library could not be prepared. Try again.');
        $('startup-retry').hidden = false;
        $('startup-retry').disabled = false;
        console.error('IZ2S startup preparation failed', e);
        return;
      }
      serverUnavailable = true;
      if (!initialRenderComplete) {
        setText($('startup-message'), 'The local server is unavailable. Try again or reopen IZ2S.');
        $('startup-retry').hidden = false;
        $('startup-retry').disabled = false;
        return;
      }
      renderServerAvailability();
      notice(
        'The local server is unavailable. Displayed device data is outdated; the panel will retry automatically.',
        'error',
        'server-unavailable',
      );
    }
  })().finally(() => { polling = null; });
  return polling;
}
document
  .querySelectorAll('[data-view]')
  .forEach((e) =>
    e.addEventListener('click', () => switchView(e.dataset.view)),
  );
document
  .querySelectorAll('[data-step]')
  .forEach((e) =>
    e.addEventListener('click', () => chooseStep(e.dataset.step)),
  );
$('recovery-enabled').addEventListener('change', () => {
  requestId = null;
  renderWizard();
});
for(const value of ['new','restore'])$('session-mode-'+value).addEventListener('click',()=>{const view=globalThis.IZ2S_PRESENTATION.connectionMode(state);if(value==='restore'&&!view.available||value==='new'&&view.retained)return;sessionModeChoice=value;renderWizard();});
$('screen-ready').addEventListener('change', renderControls);
$('recheck').addEventListener('click', () => act('/api/check', {}));
$('startup-retry').addEventListener('click', () => poll());
$('start').addEventListener('click', () => {
  requestId ||= crypto.randomUUID();
  act('/api/start', {
    kind: state?.active?.setup?.stage === 'reboot' ? 'launch' : step,
    screenReady: $('screen-ready').checked,
    requestId,
    recovery: $('recovery-enabled').checked,
    moduleIds: state.platform?.sdLauncher?[]:$('preload-enabled').checked ? [...startupSelection] : [],
    restoreInstalled: false,
  });
});
$('stop').addEventListener('click', async () => {
  const result=await act('/api/stop', {});
  if(result?.ok){requestId=null;pendingSetup=null;$('screen-ready').checked=false;chooseStep('launch');}
});
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
  dismissed = notificationKey;
  $('toast').hidden = true;
  api('/api/dismiss', {}).catch(() => {});
});
document.querySelector('.brand').addEventListener('click', (e) => {
  e.preventDefault();
  switchView('setup');
});
chooseStep('launch');
poll().finally(() => (timer = setInterval(poll, 2000)));
// No unload/visibility handler: closing or hiding Chrome never ends the session.

async function setupAction(action, extra = {}) {
  if (!pendingSetup || pendingSetup.action !== action)
    pendingSetup = { action, requestId: crypto.randomUUID() };
  const result = await act('/api/setup', { ...pendingSetup, ...extra });
  if (result) pendingSetup = null;
  return result;
}
async function retrySetup(targetStep) {
  const previousKind = (state?.active || state?.lastAttempt)?.kind;
  const nextStep = typeof targetStep === 'string' ? targetStep
    : previousKind === 'bootstrap' ? 'bootstrap'
    : previousKind === 'launch' ? 'launch' : step;
  const result = await act('/api/retry', {});
  if (result?.ok) {
    requestId = null;
    pendingSetup = null;
    setupStamp = '';
    resultStamp = '';
    $('toast').hidden = true;
    dismissed = notificationKey;
    backendNotice = '';
    $('screen-ready').checked = false;
    $('review-ready').checked = false;
    $('check-lines').checked = false;
    $('saved-ready').checked = false;
    for (let i = 0; i < 6; i++) $('metric-' + i).value = '';
    chooseStep(nextStep);
  }
}
$('verify-source').addEventListener('click', () =>
  setupAction('verify', { reviewed: $('review-ready').checked }),
);
$('skip-source').addEventListener('click', () =>
  setupAction('skip', { reviewed: $('review-ready').checked }),
);
$('reverify-source').addEventListener('click', () =>
  setupAction('verify', { reviewed: true }),
);
$('compare-source').addEventListener('click', () =>
  setupAction('compare', {
    values: Array.from({ length: 6 }, (_, i) => $('metric-' + i).value),
    linesConfirmed: $('check-lines').checked,
  }),
);
$('edit-readings').addEventListener('click', () => setupAction('edit-values'));
$('save-source').addEventListener('click', () => setupAction('save'));
$('confirm-saved').addEventListener('click', () =>
  setupAction('saved', { savedConfirmed: $('saved-ready').checked }),
);
$('retry').addEventListener('click', () => retrySetup());
$('restart-setup').addEventListener('click', () => retrySetup('bootstrap'));
for (const id of [
  'review-ready',
  'check-lines',
  'saved-ready',
  ...Array.from({ length: 6 }, (_, i) => 'metric-' + i),
])
  $(id).addEventListener('input', renderControls);
$('export-source').addEventListener('click', async () => {
  try {
    const row = state.active || state.lastAttempt,
      data = await api('/api/setup-report?id=' + encodeURIComponent(row.id)),
      url = URL.createObjectURL(
        new Blob([data.text], { type: 'text/plain;charset=utf-8' }),
      ),
      a = element('a');
    a.href = url;
    a.download = 'IZlauncher-source-check-' + row.id + '.txt';
    a.click();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
  } catch (e) {
    notice(e.message);
  }
});
$('log').addEventListener('scroll', () => {
  if (!logWriting && $('log').clientHeight)
    logFollow =
      $('log').scrollHeight - $('log').scrollTop - $('log').clientHeight < 40;
});
$('log-details').addEventListener('change', renderActivity);
$('download-session-log').addEventListener('click', () => {
  const id = (state.active || state.lastAttempt)?.id;
  if (id) downloadSessionLog(id);
});
$('download-saved-log').addEventListener('click', () => {
  if (savedLogAttempt) downloadSessionLog(savedLogAttempt);
});
document.querySelector('.activity').addEventListener('toggle', (event) => {
  if (event.target.open && logFollow)
    $('log').scrollTop = $('log').scrollHeight;
});

$('hard-stop').addEventListener('click', async () => {
  if (hardStopping) return;
  hardStopping = true;
  renderControls();
  try {
    await api('/api/hard-stop', {});
    await poll();
  } catch (e) {
    notice(e.message, 'error');
  } finally {
    hardStopping = false;
    renderControls();
  }
});


$('delivery-options').oninput = () => {
  $('screen-ready').checked = false;
  renderControls();
};
$('delivery-options').onsubmit = e => e.preventDefault();
$('delivery-options').onchange = async () => {
  if (!deliveryDirty() || busy || state.platform.locked) return;
  const loaderId = state.platform.loaderId,
    loaderSettings = {...state.platform.loaderSettings, ...fieldValues($('delivery-fields'))};
  const result = await act('/api/launch-settings', {loaderSettings});
  if (result?.ok && state.platform.loaderId === loaderId)
    state.platform.loaderSettings = loaderSettings;
  // A failed save restores the last confirmed value instead of leaving a draft.
  for (const input of $('delivery-fields').querySelectorAll('input,select'))
    delete input.dataset.savedValue;
  delete $('delivery-options').dataset.stamp;
  renderPlatform();
  renderControls();
};

$('install-slot').onchange = () => {
  $('screen-ready').checked = false;
  document.querySelectorAll('.up-ready').forEach((e) => (e.checked = false));
  act('/api/launch-settings', { installSlot: Number($('install-slot').value) });
};

$('engine-download').onclick = () =>
  act('/api/engine/install', { id: state.platform.loaderId });
$('engine-rescan').onclick = () => act('/api/engine/rescan', {});

for(const id of ['arm-setup-address','arm-setup-user','arm-setup-key']) $(id).oninput=()=>{$(id).dataset.edited='true';};
$('arm-install-confirm').onchange=()=>{if(state?.platform)renderArmSetup(state.platform);};
for(const mode of ['ssh','manual']) $('arm-method-'+mode).onclick=()=>{for(const item of ['ssh','manual']){$('arm-method-'+item).classList.toggle('selected',mode===item);$('arm-method-'+item).setAttribute('aria-pressed',String(mode===item));$('arm-'+item+'-pane').hidden=mode!==item;}};
$('arm-install-form').onsubmit=async(event)=>{
  event.preventDefault();
  $('arm-install-result').textContent='Installing the verified package over SSH…';
  const result=await act('/api/input/install',{address:$('arm-setup-address').value.trim(),user:$('arm-setup-user').value.trim(),identity:$('arm-setup-key').value.trim(),confirm:$('arm-install-confirm').checked});
  if(result?.ok){$('arm-install-result').textContent=result.keyboard?'Installed. Check the console USB connection before launch.':'Installed. Connect the USB data port to Switch and check again.';for(const id of ['arm-setup-address','arm-setup-user','arm-setup-key'])delete $(id).dataset.edited;}else $('arm-install-result').textContent='Installation was not confirmed. Check the error above, or use the manual package.';
};
$('arm-check').onclick=()=>act('/api/check', {});
async function downloadArmInstaller(profile){try{const response=await fetch('/api/input-package?format=installer&profile='+profile,{headers:{'x-iz-key':key}});if(!response.ok)throw Error((await response.json()).error);const body=await response.arrayBuffer(),standalone=(response.headers.get('content-disposition')||'').includes('.run'),installer=standalone?new Blob([body],{type:'application/octet-stream'}):new Blob([await IZ2S_ARM_INSTALLER.create(body)],{type:'application/octet-stream'});const url=URL.createObjectURL(installer),link=element('a');link.href=url;link.download=profile==='civ7'?'iz2s-civ7-arm-input.run':'iz2s-arm-input.run';link.click();setTimeout(()=>URL.revokeObjectURL(url),1000);}catch(error){notice(error.message);}}
$('arm-package-download').onclick=()=>downloadArmInstaller(state?.platform?.loaderId==='civswitch'?'civ7':'sb4');


window.addEventListener('run21:languagechange', () => {
  if (!state) return;
  historyStamp = '';
  $('log').dataset.stamp = '';
  renderHistory();
  renderActivity();
});

for (const placeholder of document.querySelectorAll('[data-folder]')) {
  const button = folderButton(placeholder.dataset.folder, null, globalThis.RUN21_LANGUAGE?.source(placeholder.textContent.trim())||placeholder.textContent.trim());
  button.id = placeholder.id;
  button.dataset.folder = placeholder.dataset.folder;
  button.className += ' ' + placeholder.className;
  button.onclick = async event => {
    event.preventDefault();
    event.stopPropagation();
    const kind = placeholder.dataset.folder;
    const id = kind === 'configuration' ? state?.platform?.configurationId
      : ['engine', 'engine-source'].includes(kind) ? state?.platform?.loaderId
      : kind === 'shell' ? state?.platform?.variant || 'izlauncher' : undefined;
    button.dataset.pending = 'true';
    button.disabled = true;
    try { await api('/api/folder/open', { kind, ...(id ? { id } : {}) }); }
    catch (error) { notice(error.message); }
    finally { delete button.dataset.pending; renderLocalActionAvailability(); }
  };
  placeholder.replaceWith(button);
}

async function downloadCardputerPackage() {
  const response=await fetch('/api/cardputer/package',{headers:{'x-iz-key':key}});
  if(!response.ok)throw Error((await response.json()).error);
  const url=URL.createObjectURL(await response.blob()),link=element('a');link.href=url;link.download='iz2s-cardputer-adv-sd.zip';link.click();setTimeout(()=>URL.revokeObjectURL(url),1000);
}
async function prepareCardputer(action){
  if(busy||cardputerRequest)return;
  cardputerRequest={id:crypto.randomUUID(),kind:action==='install'?'firmware':'package',stopping:false};
  $('cardputer-setup-result').textContent=action==='install'?'Copying to the ADV SD card…':'Preparing the SD package…';
  try{
    const result=await act('/api/cardputer/'+action,{requestId:cardputerRequest.id});
    if(result?.cancelled){$('cardputer-setup-result').textContent='Cancelled. Incomplete SD files were removed. Existing firmware and Wi-Fi settings are preserved.';return;}
    if(action==='install')$('cardputer-setup-result').textContent=result?.verified?'Copied. Start '+result.filename.replace(/^Utilities\//,'')+' from M5Launcher.':'Copy failed. Open M5Launcher USB storage and mount its SD card.';
    else if(result?.ok){await downloadCardputerPackage();$('cardputer-setup-result').textContent='Extract the ZIP to the SD card, then start IZ2S-ADV.bin in M5Launcher.';}
  }catch(error){notice(error.message);}
  finally{cardputerRequest=null;renderControls();}
}
$('cardputer-install').onclick=()=>prepareCardputer('install');
$('cardputer-file').onclick=()=>prepareCardputer('prepare');
$('cardputer-cancel').onclick=async()=>{
  const operation=cardputerRequest||state?.hardwareOperation;if(!operation||operation.stopping)return;
  operation.stopping=true;renderControls();
  $('cardputer-setup-result').textContent='Cancelling… Waiting for the current SD write to close safely. Keep ADV connected.';
  try{await api('/api/cardputer/cancel',{requestId:operation.id});await poll();}
  catch(error){operation.stopping=false;notice(error.message);}
  finally{renderControls();}
};
$('cardputer-check').onclick=()=>act('/api/check',{});
$('adv-usb-check').onclick=()=>act('/api/check',{});
$('adv-address').onclick=()=>openDeviceSettings({addresses:true,focus:'[name="cardputerEndpoint"]'});

$('library-source').addEventListener('change',()=>act('/api/launch-settings',{sdLauncher:$('library-source').value==='sd'}));
$('native-recovery-enabled').addEventListener('change',()=>act('/api/launch-settings',{nativeRecovery:$('native-recovery-enabled').checked}));
$('restore-connection').addEventListener('click',()=>act('/api/connection/restore',{}));

// Address editors are small non-modal popovers; only one is open at a time.
document.addEventListener('click',event=>{
 const selected=event.target.closest?.('.address-editor');
 for(const editor of document.querySelectorAll('.address-editor[open]'))if(editor!==selected)editor.open=false;
});
document.addEventListener('keydown',event=>{
 if(event.key!=='Escape')return;
 for(const editor of document.querySelectorAll('.address-editor[open]')){editor.open=false;editor.querySelector('summary')?.focus();}
});

} // Ordinary panel; the standalone files preview above never starts polling.
