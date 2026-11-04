'use strict';
// Local manifest eligibility only. Passing this check is not target qualification.
function candidate(app,descriptor={}, {finiteAudio=false}={}){
 const m=app.manifest;
 const no=(code,reason,userReason)=>({compatible:false,code,reason,userReason});
 if(m.builtin||m.displayOnly)return no('composed-scene','Requires a composed scene or has no executable GPU app',descriptor.capabilities?.streaming
  ? 'This module streams video from the PC. Resident transitions require a standalone GPU module.'
  : 'Resident transitions are unavailable for built-in scenes and cards without a GPU application.');
 if(m.hostRuntime){
  if(finiteAudio&&require('./services.cjs').candidate(m))return {compatible:true,code:'finite-audio-candidate',reason:'Pinned finite audio adapter; full fingerprint checked before allocation',userReason:'Experimental audio lasts up to 11 min 39 sec per session. Resident transitions on Switch are unverified.'};
  if(m.nativeLauncherReturn===true)return no('native-return-owner','Uses its own native owner and saved-menu return path, outside the shared GPU graph','This native module owns its session and menu return. Disable shared GPU RAM mode; offline support depends on the module.');
  return no('host-runtime','Host runtime requires a resident service adapter: '+m.hostRuntime,'This module service is unavailable in the shared resident session. Qualified IZDOOM and Sonic images support experimental finite audio.');
 }
 if(Object.keys(app.effects||{}).length)return no('host-audio','Host-owned sound events require a resident audio provider','This module uses PC audio. Resident audio output is unavailable.');
 if((m.sdk?.capabilities||[]).some(name=>!['pad','motion','keyboard','mouse','joy-mouse','pad-sources'].includes(typeof name==='string'?name:name.id)))
  return no('service-capability','A declared capability needs an explicit resident provider','A module capability requires a service that is unavailable in resident mode.');
 return {compatible:true,code:'gpu-candidate',reason:'GPU-only ABI-1 candidate; target execution still requires qualification',userReason:'Eligible for the graphics experiment. Switch operation and resident audio are unverified.'};
}
function assertSelection(packages,{residentFiniteAudio=false}={}){
 if(!packages.length)throw Error('Resident transitions: enable module preloading and select at least one standalone GPU module, such as IZOFF.');
 const eligibility=p=>residentFiniteAudio?p.residentAudio:p.resident;
 const excluded=packages.filter(p=>!eligibility(p)?.compatible);
 if(excluded.length)throw Error('Resident transitions: '+excluded.map(p=>p.name+' - '+(eligibility(p)?.userReason||'Compatibility is unverified.')).join(' '));
 if(packages.filter(p=>eligibility(p)?.code==='finite-audio-candidate').length>1)throw Error('Select one game version with audio per resident session.');
 return packages;
}
module.exports={candidate,assertSelection};
