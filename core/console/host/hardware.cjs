const hid=require('../abi/hid-functions.json'),services=require('../abi/service-functions.json');
const {MAGIC,VERSION,FIELD:F,FLAGS,word}=require('./layout.cjs');
const STYLES=[[1,'FullKey'],[2,'Handheld'],[4,'JoyDual'],[8,'JoyLeft'],[16,'JoyRight']];
// Sample both handheld and player-one styles every frame, including mixed attachment.
const PAD_SOURCES=[[32,'Handheld'],[0,'FullKey'],[0,'JoyDual'],[0,'JoyLeft'],[0,'JoyRight']];
const IDS=require('./controller-slots.cjs').IDS;
async function create(rpc){
 const serviceNames=['netStatus','netAddress','socketInitialized','irHandle','irStatus'];
 const api=await require('../native/client.cjs').create(rpc,{...hid,...Object.fromEntries(serviceNames.map(k=>[k,services[k]]))});
 let joyMouse,joyMouseError;
 try{joyMouse=await require('./joy-mouse.cjs').create(rpc);}catch(error){
  // An optional device may be unavailable; a lost native transport cannot be reused.
  if(require('../../session/offline-policy.cjs').isTransportLoss(error))throw error;
  joyMouseError=error.message;
 }
 for(let i=0;i<PAD_SOURCES.length;i++)await api.write(0x1000+i*4,word(PAD_SOURCES[i][0]));
 for(let id=0;id<8;id++)await api.write(0x1020+id*4,word(id));
 const settings=await require('./controller-preferences.cjs').open();
 const controllerSlots=await require('./controller-slots.cjs').create(rpc,api,{settings});
 let sequence=0,selected=null,motion=[],vibration=[],lastDiscovery=0,closed=false,sensors=null;
 async function stopMotion(){for(const handle of motion)await api.result('motionStop',[handle]);motion=[];}
 async function discover(){
  const controllers=await controllerSlots.refresh(),preference=settings.get();
  const eligible=controllers.filter(p=>preference.controllerId===null||p.id===preference.controllerId).filter(p=>preference.source==='pro'?(p.style&1)!==0:preference.source==='joycon'?(p.style&30)!==0:STYLES.some(([bit])=>p.style&bit));
  const next=eligible.find(p=>(p.style&1)!==0)??eligible[0];
  const nextStyle=next&&STYLES.find(([bit])=>(next.style&bit)&&(preference.source==='pro'?bit===1:preference.source==='joycon'?bit!==1:true));
  if(next?.id!==selected?.id||next?.style!==selected?.style||nextStyle?.[1]!==selected?.kind){
   await stopMotion();vibration=[];selected=next?{...next}:null;
   if(next){
    await api.write(0,word(next.id));const style=nextStyle;selected.kind=style[1];
    const n=await api.result('motionHandles',[api.base+0x300,2,api.base,style[0]]);if(n>2)throw Error('Invalid motion handle count');
    for(let i=0;i<n;i++){const handle=api.base+0x300+i*4;if(await api.result('motionStart',[handle])===0)motion.push(handle);}
    const v=await api.result('vibrationHandles',[api.base+0x800,2,api.base,style[0]]);if(v>2)throw Error('Invalid vibration handle count');
    for(let i=0;i<v;i++){const handle=api.base+0x800+i*4;if(await api.result('vibrationInit',[handle])===0)vibration.push(handle);}
   }
  }
  lastDiscovery=Date.now();return controllers;
 }
 const hardware={controllers:[],joyMouse,joyMouseError,async startSensors(mapped){await rpc.write(mapped+require('./layout.cjs').OFFSET+require('./pad-sweep.cjs').HEADER,require('./pad-sweep.cjs').header());sensors??=await require('./console-sensors.cjs').create({rpc,mapped});hardware.consoleSensors=sensors;},async refreshControllers(){if(closed)return;hardware.controllers=await controllerSlots.refresh();},frameCalls(mapped){
  if(closed)throw Error('Hardware context closed');
  const ext=mapped+require('./layout.cjs').OFFSET,calls=[];
  if(selected)calls.push({function:api.functions['pad'+selected.kind],args:[mapped,api.base]});
  for(let i=0;i<PAD_SOURCES.length;i++)calls.push({function:api.functions['pad'+PAD_SOURCES[i][1]],args:[ext+F.padSources+i*64,api.base+0x1000+i*4]});
  calls.push(...require('./pad-sweep.cjs').frameCalls(api.functions,api.base,ext));
  for(let i=0;i<motion.length;i++)calls.push({function:api.functions.motionRead,args:[api.base+0x400+i*4,ext+F.motion+i*96,1,motion[i]]});
  calls.push({function:api.functions.keyboard,args:[api.base+0xc00,ext+F.keyboard,1]});
  calls.push({function:api.functions.mouse,args:[ext+F.mouse]});
  if(joyMouse)calls.push(joyMouse.frameCall(mapped));
  return calls;
 },async poll(){
  if(closed)throw Error('Hardware context closed');
  if(!lastDiscovery||Date.now()-lastDiscovery>5000)hardware.controllers=await discover();
  const out=Buffer.alloc(1024);out.writeUInt32LE(MAGIC,F.magic);out.writeUInt32LE(VERSION,F.version);out.writeUInt32LE(++sequence>>>0,F.sequence);let flags=FLAGS.padSources|(joyMouse?FLAGS.joyMouse:0);
  out.writeUInt32LE(joyMouse?1:0,512);
  if(selected){
   out.writeUInt32LE(selected.id,F.padId);out.writeUInt32LE(selected.style,F.style);await api.write(0,word(selected.id));
   await api.call('pad'+selected.kind,[api.base+0x100,api.base]);(await api.read(0x100,64)).copy(out,F.pad);flags|=FLAGS.pad;
  }
  for(let i=0;i<PAD_SOURCES.length;i++){
   await api.call('pad'+PAD_SOURCES[i][1],[api.base+0x1100,api.base+0x1000+i*4]);
   (await api.read(0x1100,64)).copy(out,F.padSources+i*64);
  }
  let validMotion=0;
  for(let i=0;i<motion.length;i++){
   const result=await api.result('motionRead',[api.base+0x400,api.base+0x500,1,motion[i]]);
   if(!result&&(await api.read(0x400,4)).readUInt32LE()===1){(await api.read(0x500,96)).copy(out,F.motion+validMotion*96);validMotion++;}
  }
  if(validMotion)flags|=FLAGS.motion;out.writeUInt32LE(validMotion,F.motionCount);
  if(await api.result('keyboard',[api.base+0xc00,api.base+0xc10,1])===0&&(await api.read(0xc00,4)).readUInt32LE()===1){(await api.read(0xc10,48)).copy(out,F.keyboard);flags|=FLAGS.keyboard;}
  if(await api.result('mouse',[api.base+0xd00])===0){(await api.read(0xd00,40)).copy(out,F.mouse);flags|=FLAGS.mouse;}
  const status=await api.result('netStatus',[api.base+0xe00]);out.writeUInt32LE(status,F.network);
  if(!status){(await api.read(0xe00,16)).copy(out,F.network+4);if(await api.result('netAddress',[api.base+0xe20])===0){(await api.read(0xe20,4)).copy(out,F.network+20);flags|=FLAGS.network;}}
  if(selected){await api.write(0,word(selected.id));const handle=await api.result('irHandle',[api.base+0xf00,api.base]);out.writeUInt32LE(handle,F.ir);if(!handle){const result=await api.result('irStatus',[api.base+0xf10,api.base+0xf00]);out.writeUInt32LE(result,F.ir);if(!result)(await api.read(0xf10,4)).copy(out,F.ir+4);}}
  if(selected)await api.write(0,word(selected.id));
  out.writeUInt32LE(flags,F.flags);return out;
 },async pulse({side=0,amplitude=.15,milliseconds=120,lowFrequency=160,highFrequency=320}={}){
  if(closed)throw Error('Hardware context closed');
  return require('./haptics.cjs').play(api,vibration,{side,steps:[{amplitude,milliseconds,lowFrequency,highFrequency}]});
 },async pattern(options){
  if(closed)throw Error('Hardware context closed');
  return require('./haptics.cjs').play(api,vibration,options);
 },async close(){if(!closed){closed=true;try{await sensors?.close();}finally{try{await stopMotion();}finally{await joyMouse?.close();}}}}};
 return hardware;
}
module.exports={create,STYLES,IDS};
