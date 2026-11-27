'use strict';
// Civilization VII is the only launch path. Native runtime and the app library
// are shared with PC Link; no keyboard bootstrap or alternate game is loaded.
const path=require('node:path');
const {readJson,saveJson,createLedger}=require('../core/session/ledger.cjs');
async function create({root,settings:defaults,node,preview=false}){
 const loaders=require('../engines/index.cjs').create(root);
 const selected=loaders.select('iz2sciv-core',{});
 const configuration={id:'pc-civ7',name:'PC ↔ Civilization VII',executionHost:'pc',inputAdapter:'none',env:{},requirements:[],setupRequired:false,ui:{checks:[{id:'server',label:'PC',icon:'i-server'},{id:'lan',label:'Console',icon:'i-switch'}]}};
 const settings={...defaults,consolePort:9026};
 const connections=await require('../core/session/connections.cjs').createConnections({root,settings,networkEnabled:!preview});
 const session=require('../engines/iz2sciv-core/session.cjs').create({root,settings,node,connections,configuration,preview});
 const catalog=require('../core/packages/store-catalog.cjs'),repository=require('../core/packages/repository.cjs');
 const library=await repository.create(root,{engine:'iz2s-core',source:'current'});
 let libraryTask,libraryValue,libraryAt=0;
 async function readLibrary(){
  if(libraryValue&&Date.now()-libraryAt<2000)return libraryValue;
  if(!libraryTask)libraryTask=library.list().then(value=>{libraryValue={packages:value.packages.map(repository.publicPackage),errors:value.errors};libraryAt=Date.now();return libraryValue;}).finally(()=>{libraryTask=null;});
  return libraryTask;
 }
 const file=path.join(root,'data/state/launcher-settings.json');
 const previous=await readJson(file,{});
 if(previous.loaderId!=='iz2sciv-core'||previous.variant!=='original'||previous.schemaVersion!==4)
  await saveJson(file,{schemaVersion:4,loaderId:'iz2sciv-core',variant:'original',loaderSettings:{'iz2sciv-core':{}},installationSlots:{},sdLauncher:false,nativeRecovery:false,residentGraphics:false,residentFiniteAudio:false});
 async function idle(){if(session.ownsListener())throw Error('Wait for the current operation to finish');}
 const useLibrary=()=>{throw Error('Use the IZ2S app library to manage console applications.');};
 return {
  preview,
  refresh:(...args)=>session.refresh(...args),
  stop:(...args)=>session.stop(...args),hardStop:(...args)=>session.hardStop(...args),ownsListener:()=>session.ownsListener(),
  async snapshot(){
   const state=await session.snapshot(),apps=await require('../core/modules.cjs').metadata(root),stored=await readLibrary();
   const extensions=require('../core/extensions.cjs').catalog(root);
   return {...state,library:stored.packages,fsRpc:null,moduleCatalog:apps.modules.map(m=>m.descriptor).filter(catalog.visible),moduleErrors:[...apps.errors,...stored.errors],moduleStates:require('../core/host-modules.cjs').status(),version:22,
    platform:{civOnly:true,civswitchLdnAvailable:true,host:{platform:process.platform,architecture:process.arch},loaderId:'iz2sciv-core',variant:'original',
     consoleEndpoint:{port:9026,host:state.iz2sciv?.host||'',listener:'console'},sdLauncher:false,nativeRecovery:false,residentGraphics:false,residentFiniteAudio:false,residentAudioReady:false,
     shells:extensions.shells.items,extensionErrors:[...extensions.engines.errors,...extensions.shells.errors,...extensions.profiles.errors],loaders:loaders.list(),loaderSettings:{},engineInstalled:true,installation:null,izModules:true,baseSource:null,
     delivery:{application:'IZ2S',method:'iz2sciv-core',bootstrapName:null,baseBootstrapName:null,recoveryBootstrapName:null,preloaded:true},
     configurationId:configuration.id,configuration:{...configuration,inputDirectory:''},configurations:[configuration],profiles:{},locked:!!state.active}}
  },
  async configureLaunch(body={}){
   await idle();
   if(body.loaderId!==undefined&&body.loaderId!=='iz2sciv-core'||body.variant!==undefined&&body.variant!=='original'||body.configurationId!==undefined&&body.configurationId!=='pc-civ7')throw Error('This build supports Civilization VII only.');
   if(body.profile||body.installSlot!==undefined||body.loaderSettings&&Object.keys(body.loaderSettings).length)throw Error('Use the Civilization VII connection settings.');
   return {ok:true};
  },
  configureConnection:body=>session.configureConnection(body),iz2scivAction:body=>session.action(body),
  setupReport:(...args)=>session.setupReport(...args),historyLog:(...args)=>session.historyLog(...args),
  remove:async id=>{const ledger=createLedger(root),row=await ledger.read(id);return row?.upstream?session.remove(id):ledger.removeFailure(id);},
  dismiss:()=>session.dismiss(),loadPackages:useLibrary,unloadPackages:useLibrary,refreshSdInventory:useLibrary,
 };
}
module.exports={create};
