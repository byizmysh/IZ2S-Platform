'use strict';
// One native renderer owns the mixed menu. A PC application may borrow the
// display only after its exact request is acknowledged at QueueWaitIdle.
const assert=require('node:assert/strict');
const SHARED=8388608,MAX=15,ENTRY=112;
function catalog(applications){
 assert(Array.isArray(applications)&&applications.length<=MAX,'The mixed menu supports 15 applications');const bytes=Buffer.alloc(MAX*ENTRY),ids=new Set();
 applications.forEach((app,i)=>{assert(/^[a-z0-9][a-z0-9-]{0,47}$/.test(app.id)&&!ids.has(app.id),'Invalid mixed-menu host identity');assert(typeof app.name==='string'&&app.name.length>0&&!app.name.includes('\0'),'Invalid mixed-menu host name');ids.add(app.id);let name='';for(const ch of app.name){if(Buffer.byteLength(name+ch)>64)break;name+=ch;}bytes.write(app.id,i*ENTRY,48);bytes.write(name,i*ENTRY+48,64);});
 return {count:applications.length,bytes};
}
function create({rpc,context,layout:L,controls,parentSnapshot,imageCount,status,supervisorRange,menuCommands,closed=()=>false,pause=ms=>new Promise(r=>setTimeout(r,ms)),clock=Date.now,timeoutMs=15000,save=async()=>{}}){
 assert(Number.isSafeInteger(context)&&context>0&&Number.isSafeInteger(parentSnapshot)&&parentSnapshot>0&&Number.isInteger(imageCount)&&imageCount>0&&imageCount<=8);
 const report={state:'native',leases:0,publications:0,hostSnapshotTransferBytes:0};let lease,pending=false,retained=false,lastClaim=0,delegation=null;
 async function persist(){try{await save(report);}catch(error){report.telemetryError=error.message;}}
 const word=value=>{const bytes=Buffer.alloc(4);bytes.writeUInt32LE(value);return bytes;};
 function healthy(state){assert(!closed()&&state.state===2&&!state.poisoned&&state.host_mode===1,'The mixed native menu is unavailable');}
 async function mutate(action,mayRefuse=false){assert(!retained&&!pending,'The previous mixed-menu operation is unverified');pending=true;try{const result=await action();pending=false;return result;}catch(error){if(mayRefuse&&(error.displayNotGranted||error.framesNotAccepted)){pending=false;throw error;}retained=true;report.state='retained';throw Object.assign(error,{resourcesRetained:true});}}
 function observeFrames(state){
  if(delegation&&state.host_frames_returned===delegation.ticket&&!state.host_frames_active&&!state.host_frames_paused){report.lastFrameReturn={...delegation,reason:state.host_frames_reason,graphicsIdle:true,resourcesReleased:false};delegation=null;report.state=lease?'host':'native';}
  return delegation;
 }
 async function waitFrames(token,kind){const until=clock()+timeoutMs;for(;;){const state=await status();healthy(state);assert(state.host_display_ready===lease?.token,'The host frame publication lost its display lease');
  if(kind==='offer'&&state.host_frames_ack===token){if(state.host_frames_error)throw Object.assign(Error('Native host frame plan rejected (0x'+state.host_frames_error.toString(16)+')'),{framesNotAccepted:true});return state;}
  if(kind==='cancel'&&state.host_frames_returned===token&&!state.host_frames_active&&!state.host_frames_paused)return state;
  assert(clock()<until,'Native host frame ownership was not acknowledged');await pause(10);
 }}
 async function wait(token,ready,revision){const until=clock()+timeoutMs;for(;;){const state=await status();healthy(state);
   if(ready){
    if(state.host_display_request===token&&state.host_display_ready===token&&state.host_display_release!==token)return state;
    if(state.host_display_rejected===token||(token&1)&&(state.host_display_request!==token||state.host_display_release===token))throw Object.assign(Error('The console kept its display for the SD menu or application'),{menuBusy:true,displayNotGranted:true});
   }else if(state.host_display_ready===0&&state.host_display_release===token&&(revision===undefined||state.host_applied===revision))return state;
   assert(clock()<until,'Mixed-menu display ownership was not acknowledged');await pause(10);
  }}
 function owned(token,state){
  let active=true,releasing=false,framesOffered=false,revision=state.host_revision;report.state='host';report.leases++;
  const application=state.applications?.[(state.host_launch||state.host_remove)-1];
  const result={token,request:{launch:state.host_launch||0,remove:state.host_remove||0,...(application?{application}:{})},pendingInstall:state.requestStable&&state.host_install_sequence?{sequence:state.host_install_sequence,index:state.host_install_index,target:state.host_install_target}:null,get active(){return active&&!closed()&&!retained;},
   async acknowledgeInstall(sequence){assert(result===lease&&result.active&&!releasing&&result.pendingInstall?.sequence===sequence,'The mixed install request changed');await mutate(()=>rpc.write(context+L.host_install_ack,word(sequence)));},
   async publish({applications,menu}){
    assert(result===lease&&result.active&&!releasing,'The host does not own mixed-menu graphics');const plan=catalog(applications);
    assert(!delegation,'Stop the delegated application before replacing its menu');
    assert(menu?.commands?.[0]?.length===imageCount&&BigInt(menu.layout)>0n&&BigInt(menu.controls?.descriptor)>0n,'Mixed-menu graphics metadata differs');assert(revision<0xffffffff,'Mixed-menu publication generation exhausted');
    const stateBytes=await rpc.read(controls,1024);assert(stateBytes.readUInt32LE(112)===plan.count&&stateBytes.readUInt32LE(1020)===plan.count&&!stateBytes.readUInt32LE(1012)&&!stateBytes.readUInt32LE(1016),'Publish the restored host menu before returning native ownership');
    await mutate(async()=>{
     // The source is already in the console's coherent buffer. No 8 MiB
     // read/upload is added to the host transition.
     await rpc.call(0x586fb0,[parentSnapshot,controls,SHARED]);
     await rpc.write(context+L.host_catalog,plan.bytes);await rpc.write(context+L.host_count,word(plan.count));
     const graphics=Buffer.alloc(16),commands=Buffer.alloc(64);graphics.writeBigUInt64LE(BigInt(menu.layout));graphics.writeBigUInt64LE(BigInt(menu.controls.descriptor),8);menu.commands[0].forEach((handle,i)=>commands.writeBigUInt64LE(BigInt(handle),i*8));
     await rpc.write(context+L.menu_layout,graphics);await rpc.write(context+L.menu_commands,commands);await rpc.write(context+L.host_revision,word(++revision));
    });report.publications++;await persist();
   },
   async delegateFrames({id,controls:appControls,commands}){
    assert(result===lease&&result.active&&!releasing&&!delegation&&!framesOffered,'Host frame registration requires a fresh exclusive display lease');
    const frames=require('./host-frames.cjs'),bytes=frames.plan({id,controls:appControls,commands,imageCount,menuControls:controls,supervisorRange,menuCommands});
    // The launcher loader owns these buffers. Never trust an app manifest
    // pointer or infer ownership merely from the address being nonzero.
    try{await frames.verifyControls(rpc,appControls);}catch(error){if(error.code!=='ERR_ASSERTION'||error.resourcesRetained){retained=true;report.state='retained';error.resourcesRetained=true;}throw error;}
    framesOffered=true;await mutate(async()=>{await rpc.write(context+L.host_frames_plan,bytes);await rpc.write(context+L.host_frames_offer,word(token));await waitFrames(token,'offer');},true);
    delegation={ticket:token,id,controls:appControls.mapped};report.delegatedFrames={...delegation};await persist();return {...delegation};
   },
   async cancelFrames(){
    assert(result===lease&&result.active&&!releasing&&delegation,'No delegated application is owned by this display lease');const ticket=delegation.ticket;
    await mutate(async()=>{await rpc.write(context+L.host_frames_cancel,word(ticket));observeFrames(await waitFrames(ticket,'cancel'));});await persist();
   },
   async release(){
    assert(result===lease&&result.active&&!releasing,'Mixed-menu display lease is unavailable');releasing=true;
    await mutate(async()=>{await rpc.write(context+L.host_display_release,word(token));observeFrames(await wait(token,false,revision));});active=false;lease=null;report.state=delegation?'delegated':'native';await persist();
   }};lease=result;return result;
 }
 async function acquire(){
  assert(!lease&&!retained&&!pending,'A mixed-menu display lease is already held');let state;
  try{state=await status();}catch(error){throw Object.assign(error,{displayNotClaimed:true});}healthy(state);observeFrames(state);
  if(state.host_display_ready){retained=true;report.state='retained';throw Object.assign(Error('Another operation holds mixed-menu graphics'),{resourcesRetained:true});}
  if(state.host_display_request!==state.host_display_release){
   assert((state.host_display_request&1)&&((state.host_launch>0)!==(state.host_remove>0)),'Another operation holds mixed-menu graphics');const selected=state.applications?.[state.host_launch-1];assert((state.host_launch||state.host_remove)<=state.host_count||state.host_launch>0&&selected?.source==='sd'&&selected.execution==='pc','The mixed-menu request is outside the host catalog');
   const token=state.host_display_request;return mutate(async()=>{await rpc.write(context+L.host_display_claim,word(token));state=await wait(token,true);return owned(token,state);},true);
  }
  if(state.active||state.load_stage>0&&state.load_stage<8&&!state.last_app_error)throw Object.assign(Error('The SD application is still running or loading'),{menuBusy:true,activeApplication:{id:state.last_id,source:'sd'}});
  // Only the native thread publishes request/ready. Disjoint token parity
  // prevents a delayed PC claim from becoming a different console action.
  const token=lastClaim=Math.max(lastClaim,...['host_display_request','host_display_release','host_display_claim','host_display_rejected','host_display_observed'].map(key=>Math.floor((state[key]||0)/2)*2))+2;assert(token<=0xfffffffe,'Mixed-menu display generation exhausted');
  return mutate(async()=>{await rpc.write(context+L.host_display_claim,word(token));state=await wait(token,true);return owned(token,state);},true);
 }
 return {acquire,report,observeFrames,get delegated(){return delegation?{...delegation}:null;},get active(){return lease?.active===true;},get retained(){return retained;}};
}
module.exports={MAX,ENTRY,SHARED,catalog,create};
