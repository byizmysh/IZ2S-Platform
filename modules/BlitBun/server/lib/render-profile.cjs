// One display policy for browser capture and native-window game streams.
// Operation mode and network route are independent: TV over Wi-Fi is still TV.
const PROFILES=Object.freeze({wifi:Object.freeze({mode:'wifi',width:1280,height:720,fps:60}),lan:Object.freeze({mode:'lan',width:1920,height:1080,fps:60})});
const RESOLUTIONS=Object.freeze(['auto','640','832','960','1280','1600','1920']),FRAME_RATES=Object.freeze([30,40,60]);
function forMode(mode){return PROFILES[mode]||PROFILES.wifi;}
function normalizeSelection(value={}){const modeSize=(v,fallback)=>RESOLUTIONS.slice(1).includes(String(v))?String(v):fallback;return {allowDockWifi:value.allowDockWifi===true,compressionPolicy:['auto','on','off'].includes(value.compressionPolicy)?value.compressionPolicy:'auto',renderMode:['auto','direct','ram'].includes(value.renderMode)?value.renderMode:'auto',resolution:RESOLUTIONS.includes(String(value.resolution))?String(value.resolution):'auto',dockResolution:modeSize(value.dockResolution,'1920'),handheldResolution:modeSize(value.handheldResolution,'1280'),fps:FRAME_RATES.includes(Number(value.fps))?Number(value.fps):60};}
function resolver({initialMode=process.env.IZ12_CONSOLE_MODE||'wifi',links,consoleState,selection=()=>({})}={}){
 if(!links)try{links=JSON.parse(process.env.IZ12_CONSOLE_LINKS||'[]');}catch{links=[];}
 let current=forMode(consoleState?'wifi':initialMode),observedDisplay=false;
 return address=>{
  const state=consoleState?.();
  if(state&&!state.stale&&['docked','handheld'].includes(state.mode)){observedDisplay=true;current={...forMode(state.mode==='docked'?'lan':'wifi'),displayMode:state.mode,profileBasis:'console-operation-mode'};}
  else if(!consoleState&&!observedDisplay){const mode=(links||[]).find(p=>p.address===address)?.mode;if(PROFILES[mode])current=PROFILES[mode];}
  // Keep the last observed physical mode through a transient stale sample.
  // Manual output size changes do not erase it or manufacture a route change.
  const chosen=normalizeSelection(selection()),result={...current,fps:chosen.fps};
  const width=Number(chosen.resolution==='auto'?(current.mode==='lan'?chosen.dockResolution:chosen.handheldResolution):chosen.resolution);
  result.width=width;result.height=Math.round(width*9/16/4)*4;
  if(chosen.resolution!=='auto')result.profileBasis='manual-resolution';
  else if(width!==current.width)result.profileBasis='configured-mode-resolution';
  return result;
 };
}
async function settings(root){
 const fs=require('node:fs/promises'),path=require('node:path'),file=path.join(require(path.join(root,'core/module-paths.cjs')).home(root,'blitbun'),'data/render-settings.json');let value;
 try{value=normalizeSelection(JSON.parse(await fs.readFile(file,'utf8')));}catch{value=normalizeSelection();}
 return {get:()=>({...value}),async set(next){const selected=normalizeSelection({...value,...next}),temp=file+'.'+require('node:crypto').randomUUID()+'.tmp';await fs.mkdir(path.dirname(file),{recursive:true});try{await fs.writeFile(temp,JSON.stringify(selected,null,2)+'\n',{mode:0o600,flag:'wx'});await fs.rename(temp,file);value=selected;}finally{await fs.rm(temp,{force:true});}return {...value};}};
}
module.exports={PROFILES,RESOLUTIONS,FRAME_RATES,forMode,normalizeSelection,resolver,settings};
