'use strict';
// Validate the complete destination selection before creating any durable job.
function sessionEligibility(manifest,{engine=process.env.IZ15_LOADER}={}){
 if(engine&&Array.isArray(manifest.requirements?.engines)&&!manifest.requirements.engines.includes(engine))return {compatible:false,reason:'This experiment is available in SmileBASIC 4 only'};
 if(['civswitch','civswitch-ldn'].includes(engine)&&(manifest.native||manifest.localRuntime))return {compatible:false,reason:'This native application needs a Civ7 executor'};
 if(manifest.sdOnly)return {compatible:false,reason:'This application must be installed on SD'};
 if(manifest.native&&!manifest.hostRuntime||manifest.localRuntime&&!manifest.hostRuntime)return {compatible:false,reason:'This application requires the autonomous SD launcher'};
 return {compatible:true};
}
function consoleMask(packages,{storageReady=false}={}){
 let sd=0,session=0;for(const [i,pkg]of packages.slice(0,16).entries()){
  if(pkg.supported===false)continue;
  if(storageReady)sd|=1<<i;
  if(pkg.session?.compatible!==false)session|=1<<i;
 }
 return ((session<<16)|sd)>>>0;
}
function consoleTarget(state){const value=state.readUInt32LE(792);if(value===0||value===1)return 'session';if(value===2)return 'sd';throw Error('Unknown console installation destination');}
function create(packages,ids,{target,targets={},sdMode=false,storageReady=false,residentGraphics=false}={}){
 if(!Array.isArray(ids)||!ids.length||ids.length>16||ids.some(id=>typeof id!=='string'||!/^[a-z0-9][a-z0-9-]{0,47}$/.test(id)))throw Error('Select available applications');
 if(!targets||typeof targets!=='object'||Array.isArray(targets)||Object.keys(targets).some(id=>!ids.includes(id)))throw Error('Invalid application destinations');
 const rows=[...new Set(ids)].map(id=>{
  const pkg=packages.find(p=>p.id===id),destination=Object.hasOwn(targets,id)?targets[id]:target??(sdMode?'sd':'session');
  if(!['sd','session'].includes(destination))throw Error('Unknown application destination');
  if(!pkg||pkg.supported===false)throw Error('Application is unavailable for this launcher: '+id);
  if(destination==='sd'){
   if(!storageReady)throw Error('The console SD connection is not ready');
   if(residentGraphics&&!sdMode)throw Error('SD installation requires the ordinary launcher menu');
  }else{
   if(sdMode)throw Error('Session applications require the ordinary launcher mode');
   if(pkg.session?.compatible===false)throw Error(pkg.name+': '+pkg.session.reason);
  }
  return {pkg,target:destination};
 });
 if(rows.filter(row=>row.target==='sd').length>15)throw Error('Choose up to 15 SD applications');
 return rows;
}
module.exports={create,sessionEligibility,consoleMask,consoleTarget};
