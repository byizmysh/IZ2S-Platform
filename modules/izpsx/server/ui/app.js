'use strict';
const $=id=>document.getElementById(id),key=location.hash.slice(1),titles={games:'Games',bios:'BIOS',saves:'Memory cards'};
let area='games',relative='',busy=false,uploading=false,ready=false,attached=false,cached=null,uploadToken=null,cancel=false,pendingCanCancel=true,statusSequence=0,pendingUpload=null,connectionSession=null;
async function api(route,data){const r=await fetch(route,{method:data===undefined?'GET':'POST',headers:{'x-izpsx-key':key,...(data===undefined?{}:{'Content-Type':'application/json'})},...(data===undefined?{}:{body:JSON.stringify(data)})});const v=await r.json();if(!r.ok)throw Error(v.error||'The action could not be completed');return v;}
function controls(){
 document.body.setAttribute('aria-busy',String(busy||uploading));const locked=busy||uploading||!!uploadToken;
 $('connect').disabled=locked||!ready;$('disconnect').disabled=locked||!attached||!ready;$('refresh').disabled=locked||!ready;
 $('upload').disabled=locked||!ready||area==='saves';$('upload-folder').disabled=locked||!ready||area!=='games';$('mkdir').disabled=locked||!ready||area!=='games';$('up').disabled=locked||!ready||!relative;
 $('search').disabled=busy||uploading;$('sort').disabled=busy||uploading;
 document.querySelectorAll('[data-area],#files button').forEach(b=>b.disabled=locked||!ready);
 $('upload-resume').hidden=!pendingUpload?.resumeAllowed||uploading;$('upload-resume').disabled=busy||!ready;
 $('upload-cancel').disabled=uploading?false:busy||!ready||!uploadToken||!pendingCanCancel;$('console-dot').dataset.ready=String(ready);
}
async function action(task){if(busy)return;busy=true;$('notice').dataset.state='';$('notice').dataset.source='';$('notice').textContent='';controls();try{await task();}catch(e){$('notice').dataset.state='error';$('notice').dataset.source=/FS RPC|ECONN|file session|SD storage|console session/i.test(e.message)?'connection':'action';$('notice').textContent=e.message;}finally{busy=false;controls();}}
const size=n=>n>=1073741824?(n/1073741824).toFixed(2)+' GB':n>=1048576?(n/1048576).toFixed(1)+' MB':n>=1024?(n/1024).toFixed(1)+' KB':n+' B';
const type=f=>f.type==='dir'?'FOLDER':f.name.includes('.')?f.name.split('.').pop().toUpperCase():'FILE';
function button(label,handler,cls='quiet'){const b=document.createElement('button');b.textContent=label;b.className=cls;b.onclick=handler;return b;}
function draw(){
 if(!cached)return;$('files').replaceChildren();$('path').textContent=cached.path.replace(/^\/sd/,'SD:');$('breadcrumb').textContent=titles[area]+(relative?' / '+relative:'');
 const query=$('search').value.trim().toLocaleLowerCase(),entries=cached.entries.filter(f=>f.name.toLocaleLowerCase().includes(query)).slice();
 entries.sort((a,b)=>(a.type==='dir'?0:1)-(b.type==='dir'?0:1)||($('sort').value==='size'?b.size-a.size:0)||a.name.localeCompare(b.name,undefined,{numeric:true}));
 $('file-count').textContent=(query?entries.length+' of '+cached.entries.length:entries.length)+' items';
 for(const f of entries){
  const row=document.createElement('article');row.className='file';const mark=document.createElement('div');mark.className='file-icon';mark.textContent=type(f).slice(0,4);mark.setAttribute('aria-hidden','true');
  const info=document.createElement('div'),name=document.createElement('strong'),detail=document.createElement('small');name.textContent=f.name;name.translate=false;detail.textContent=type(f)+(f.type==='file'?' · '+size(f.size):'');info.append(name,detail);row.append(mark,info);
  if(f.type==='dir')row.append(button('Open',()=>action(async()=>{relative=relative?relative+'/'+f.name:f.name;$('search').value='';await browse();}),'accent'));
  if(area==='games')row.append(button('Delete',()=>action(async()=>{
   const question=f.type==='dir'?'Permanently delete the game folder “'+f.name+'” and every file inside it?':'Permanently delete “'+f.name+'”? If it is part of a CUE game, the game may stop working.';
   if(!window.confirm(question))return;await api('/api/delete',{area,relative,name:f.name,type:f.type,size:f.size,confirmed:f.name});$('notice').textContent='Removed from SD: '+f.name;await browse();
  }),'delete'));
  $('files').append(row);
 }
 if(!entries.length){const box=document.createElement('div');box.className='empty';const h=document.createElement('h3'),p=document.createElement('p');h.textContent=query?'No matches':'This folder is empty';p.textContent=query?'Try a different name.':area==='games'?'Upload a game folder or choose CUE and all of its tracks.':area==='bios'?'Upload your own 512 KB BIOS as scph101.bin.':'Memory cards appear after you start your first game.';box.append(h,p);$('files').append(box);}
 controls();
}
async function browse(){cached=await api('/api/files',{area,relative});attached=true;draw();}
async function status(){
 const sequence=++statusSequence;let s;try{s=await api('/api/status');}catch(error){if(sequence!==statusSequence)return;throw error;}if(sequence!==statusSequence)return s;const changed=connectionSession!==null&&connectionSession!==(s.session||null);connectionSession=s.session||null;
 if(changed){cached=null;relative='';$('files').replaceChildren();$('notice').textContent='';$('notice').dataset.state='';$('notice').dataset.source='';}
 if($('notice').dataset.source==='status'||s.attached&&$('notice').dataset.source==='connection'){$('notice').textContent='';$('notice').dataset.state='';$('notice').dataset.source='';}
 ready=s.ready;attached=s.attached===true;
 if(!uploading){pendingUpload=s.pendingUpload||null;uploadToken=s.pendingUpload?.token||null;$('upload-progress').hidden=!uploadToken;pendingCanCancel=s.pendingUpload?.cancelAllowed!==false;if(uploadToken){$('upload-progress').hidden=false;$('upload-name').textContent=s.pendingUpload.name;$('upload-detail').textContent=(s.pendingUpload.reason||'Upload paused. Choose the same file to continue.')+(s.pendingUpload.active?' '+size(s.pendingUpload.offset||0)+' / '+size(s.pendingUpload.bytes):'');$('upload-meter').value=(s.pendingUpload.offset||0)/s.pendingUpload.bytes;}}
 $('console-state').textContent=ready?'IZlauncher ready':'Console unavailable';$('status').textContent=s.reason||'Browse your PS1 collection or upload games from your PC.';controls();return s;
}
async function upload(files,folder=false,resume=null){
 if(!files.length)return;if(uploadToken&&!resume)throw Error('Continue or cancel the unfinished upload first');
 const baseArea=resume?resume.area:area,plan=resume?{items:[{file:files[0],name:resume.name}]}:await IzpsxImport.prepare(area,relative,files,folder,cached?.entries),items=plan.items;
 async function digest(bytes){return [...new Uint8Array(await crypto.subtle.digest('SHA-256',bytes))].map(b=>b.toString(16).padStart(2,'0')).join('');}
 async function identity(file){const a=new Uint8Array(await file.slice(0,65536).arrayBuffer()),b=new Uint8Array(await file.slice(Math.max(0,file.size-65536),file.size).arrayBuffer()),all=new Uint8Array(a.length+b.length+8);all.set(a);all.set(b,a.length);new DataView(all.buffer).setBigUint64(a.length+b.length,BigInt(file.size));return digest(all);}
 const total=items.reduce((n,item)=>n+item.file.size,0);let sent=0,failure;uploading=true;cancel=false;$('upload-progress').hidden=false;controls();
 try{for(const {file:f,name}of items){
  if(cancel)throw Error('Upload cancelled');$('upload-name').textContent=name;const fingerprint=await identity(f);if(resume&&(f.name!==resume.name.split('/').pop()||f.size!==resume.bytes||fingerprint!==resume.fingerprint))throw Error('Choose the original file with the same name and size');
  let opened,verificationTimer,checking=!!resume;
  if(resume){$('upload-detail').textContent='Verifying saved data on SD…';$('upload-meter').removeAttribute('value');verificationTimer=setInterval(async()=>{try{const p=await api('/api/upload/resume-progress');if(!checking)return;if(p.phase==='verifying'){$('upload-detail').textContent='Verifying saved data: '+size(p.bytes)+' / '+size(p.total);if(p.total)$('upload-meter').value=p.bytes/p.total;}}catch{}},1000);}
  try{opened=resume?await api('/api/upload/resume',{token:resume.token,name,size:f.size,fingerprint}):await api('/api/upload/begin',{area:baseArea,name,size:f.size,fingerprint});}
  finally{checking=false;if(verificationTimer)clearInterval(verificationTimer);}
  uploadToken=opened.token;
  if(resume){for(const part of opened.chunks||[]){$('upload-detail').textContent='Checking source file: '+size(part.offset)+' / '+size(opened.bytes||0);const bytes=await f.slice(part.offset,part.offset+part.bytes).arrayBuffer();if(await digest(bytes)!==part.sha256)throw Error('The selected file differs from the uploaded data. Choose the original file');}sent=opened.bytes||0;}
const chunkBytes=Number.isInteger(opened.chunkBytes)&&opened.chunkBytes>0&&opened.chunkBytes<=0x70000?opened.chunkBytes:32768;
  for(let offset=opened.bytes||0;offset<f.size;){if(cancel)throw Error('Upload cancelled');const bytes=new Uint8Array(await f.slice(offset,offset+chunkBytes).arrayBuffer());let binary='';for(let at=0;at<bytes.length;at+=8192)binary+=String.fromCharCode(...bytes.subarray(at,at+8192));await api('/api/upload/chunk',{token:uploadToken,offset,data:btoa(binary)});offset+=bytes.length;sent+=bytes.length;$('upload-meter').value=sent/total;$('upload-detail').textContent=size(sent)+' / '+size(total);}
  if(cancel)throw Error('Upload cancelled');await api('/api/upload/finish',{token:uploadToken});uploadToken=null;
 }$('notice').textContent='Upload complete.'+(plan.repaired?' CUE paths corrected in the SD copy.':'')+(plan.skipped?' Skipped '+plan.skipped+' non-game files.':'')+' You can now open IZPSX.';
 }catch(e){failure=e;$('upload-detail').textContent='Upload paused: '+e.message;if(uploadToken&&cancel)try{await api('/api/upload/cancel',{token:uploadToken});uploadToken=null;pendingUpload=null;}catch(cleanup){if(!e.message.includes(cleanup.message))e.message+=' '+cleanup.message;}throw e;}
 finally{uploading=false;if(!uploadToken)$('upload-progress').hidden=true;try{await status();if(!uploadToken)await browse();}catch(e){if(!failure)$('notice').textContent=e.message;}if(failure&&uploadToken)$('upload-detail').textContent='Upload paused: '+failure.message;controls();}
}
$('connect').onclick=()=>action(async()=>{$('console-state').textContent='Connecting SD…';$('status').textContent='Opening console SD storage…';cached=await api('/api/connect',{area,relative});attached=true;draw();await status();});
$('disconnect').onclick=()=>action(async()=>{const r=await api('/api/disconnect',{});attached=false;$('notice').textContent=r.shared?'This manager has disconnected. The shared file service remains available.':'SD disconnected. You can now open IZPSX on the console.';await status();});
$('refresh').onclick=()=>action(browse);$('up').onclick=()=>action(async()=>{relative=relative.split('/').slice(0,-1).join('/');await browse();});
$('mkdir').onclick=()=>action(async()=>{const name=window.prompt('New game folder name');if(name===null)return;if(!name.trim())throw Error('Enter a folder name');await api('/api/mkdir',{area:'games',name:[relative,name].filter(Boolean).join('/')});await browse();});
$('upload').onclick=()=>{$('file-picker').accept=area==='games'?'.cue,.bin,.img,.pbp,.sbi,.sub,.ccd':'.bin';$('file-picker').click();};
$('file-picker').onchange=()=>{const files=[...$('file-picker').files];$('file-picker').value='';action(()=>upload(files));};
$('upload-resume').onclick=()=>$('resume-picker').click();$('resume-picker').onchange=()=>{const files=[...$('resume-picker').files];$('resume-picker').value='';if(files.length)action(()=>upload(files,false,pendingUpload));};
$('upload-folder').onclick=()=>$('folder-picker').click();$('folder-picker').onchange=()=>{const files=[...$('folder-picker').files];$('folder-picker').value='';action(()=>upload(files,true));};
$('upload-cancel').onclick=()=>{if(uploading){cancel=true;$('upload-detail').textContent='Cancelling after the current chunk…';}else action(async()=>{await api('/api/upload/cancel',{token:uploadToken});uploadToken=null;$('upload-progress').hidden=true;await status();await browse();});};
$('search').oninput=draw;$('sort').onchange=draw;
const tabs=[...document.querySelectorAll('[data-area]')];for(const b of tabs){b.onclick=()=>action(async()=>{area=b.dataset.area;relative='';$('section-title').textContent=titles[area];$('search').value='';$('hint').textContent=area==='games'?'Keep each CUE together with its BIN/IMG tracks in a game folder.':area==='bios'?'Your own 512 KB SCPH-101 BIOS.':'Two shared PS1 cards. Read-only here to protect your progress.';for(const t of tabs){const selected=t===b;t.classList.toggle('selected',selected);t.setAttribute('aria-selected',String(selected));t.tabIndex=selected?0:-1;}$('files-panel').setAttribute('aria-labelledby',b.id);await browse();});b.onkeydown=e=>{if(!['ArrowLeft','ArrowRight','Home','End'].includes(e.key)||busy||uploading||!ready||uploadToken)return;e.preventDefault();const i=tabs.indexOf(b),next=e.key==='Home'?0:e.key==='End'?tabs.length-1:(i+(e.key==='ArrowRight'?1:-1)+tabs.length)%tabs.length;tabs[next].focus();tabs[next].click();};}
async function initialize(){await status();if(ready)await action(async()=>{$('console-state').textContent='Connecting SD…';$('status').textContent='Opening console SD storage…';await browse();});}
function statusFailure(e){ready=false;$('notice').dataset.source='status';$('notice').dataset.state='error';$('notice').textContent=e.message;controls();}
initialize().catch(statusFailure);setInterval(()=>{if(!busy&&!uploading)status().catch(statusFailure);},3000);
