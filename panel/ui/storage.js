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
