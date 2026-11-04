'use strict';
// Prepares ordinary app-owned Vulkan resources before the frame worker starts.
// Does not modify a loading engine, transport protocol or native execution code.
const fs=require('node:fs/promises'),path=require('node:path');
const {struct,record}=require('./record.cjs'),L=require('./layout.cjs');
const C=require('../../../core/services/app-catalog.cjs');
const check=(value,message)=>{if(!value)throw Error(message);};
async function prepare({rpc,menu,directory,catalog,finiteAudio=false,shell='izlauncher',root=path.resolve(__dirname,'../../..'),prepareApp=require('../../../core/services/gpu-app.cjs').prepare}){
 const source=require('../../../tools/resident/prepare.cjs'),menuDirectory=source.menuRoot(shell,root);
 directory=path.resolve(directory);const reportFile=path.join(directory,'resident-session.json'),report={version:1,session:rpc.a.session,ready:false,consoleExecuted:false,
  started:new Date().toISOString(),mode:'experimental-resident-graphics',shell,audioProviderReady:false,resourcesRetained:true};
 const save=()=>require('../../../core/session/ledger.cjs').saveJson(reportFile,report);
 await save();const supported=[],excluded=[];
 for(const entry of catalog.apps){const app=await C.readApp(entry.root),candidate=source.candidate(app,{}, {finiteAudio});if(candidate.compatible)supported.push(app.root);else excluded.push({id:app.id,reason:candidate.reason});}
 report.excluded=excluded;await save();
 if(!supported.length||excluded.length){report.error=!supported.length?'No GPU-only apps are available for the resident graphics experiment':'Some selected apps require unavailable resident services';await save();throw Error(report.error+(excluded.length?': '+excluded.map(a=>a.id+' - '+a.reason).join('; '):''));}
 const bundle=path.join(directory,'resident-bundle');
 let manifest=await source.prepare(bundle,supported,{qualifyFiniteAudio:finiteAudio,shell});report.bundle=manifest;report.excluded=excluded;await save();
 const module=JSON.parse(await fs.readFile(path.join(directory,'compatibility-module.json'))),display=JSON.parse(await fs.readFile(path.join(directory,'vulkan-display.json')));
 const worker=await require('../../../core/services/native-six.cjs').create(rpc,directory),functions={};
 const p=rpc.a.scratch+0x10000,saved=await rpc.read(p,0x10000),put=(offset,b)=>rpc.write(p+offset,b),ptr=async offset=>(await rpc.read(p+offset,8)).readBigUInt64LE();
 async function call(name,...args){
  if(!functions[name]){const symbol=module.symbols.find(s=>s.name===name&&s.section&&s.type===2);check(symbol,'Missing Vulkan function '+name);
   const address=module.base+symbol.offset;await rpc.request({command:'verify-function',address,hex:(await rpc.read(address,32)).toString('hex')});functions[name]=address;}
  return args.length>4?worker.call(functions[name],args):rpc.callAddress(functions[name],args).then(r=>r.result);
 }
 const ok=async(name,...args)=>check(BigInt.asIntN(32,await call(name,...args))===0n,name+' failed');
 const device=BigInt(menu.resources.device);let properties,physical;
 async function buffer(bytes,usage=0x20,initial=Buffer.alloc(0),allowLarge=false){
  const profiles=require('../../../core/services/private-memory-profile.cjs');
  if(allowLarge&&bytes>profiles.STANDARD_BYTES)bytes=Math.ceil(bytes/4096)*4096;
  check(Number.isSafeInteger(bytes)&&bytes>=4&&bytes<=(allowLarge?profiles.RESIDENT_AUDIO_BYTES:profiles.STANDARD_BYTES)&&Buffer.isBuffer(initial)&&initial.length<=bytes,'Invalid resident buffer');
  await put(0x100,struct(56,12,{32:usage},{24:bytes}));await ok('vkCreateBuffer',device,p+0x100,0,p);const handle=await ptr(0);
  await call('vkGetBufferMemoryRequirements',device,handle,p+0x200);const requirements=await rpc.read(p+0x200,24),allocationBytes=requirements.readBigUInt64LE(),bits=requirements.readUInt32LE(16);let type=-1;
  for(let i=0;i<properties.readUInt32LE();i++)if((bits&(1<<i))&&(properties.readUInt32LE(4+i*8)&6)===6){type=i;break;}
  check(type>=0&&allocationBytes>=BigInt(bytes)&&allocationBytes<=BigInt(bytes+65536),'Resident coherent storage unavailable');
  await put(0x100,struct(32,5,{24:type},{16:allocationBytes}));await ok('vkAllocateMemory',device,p+0x100,0,p);const memory=await ptr(0);await ok('vkBindBufferMemory',device,handle,memory,0);
  await put(0x100,struct(48,1000271000,{}, {24:memory,40:allocationBytes}));await ok('vkMapMemory2',device,p+0x100,p);const mapped=Number(await ptr(0));check(Number.isSafeInteger(mapped)&&mapped>0,'Invalid resident mapping');
  await require('../../../core/services/owned-buffer.cjs').initialize(rpc,mapped,bytes,initial,bytes>profiles.STANDARD_BYTES?{bytes,profile:'resident-audio-128'}:undefined);
  return {buffer:String(handle),memory:String(memory),mapped,bytes,allocationBytes:String(allocationBytes)};
 }
 async function descriptorLayout(count,push=false){
  const bindings=Buffer.alloc(count*24);for(let i=0;i<count;i++){bindings.writeUInt32LE(i,i*24);bindings.writeUInt32LE(7,i*24+4);bindings.writeUInt32LE(1,i*24+8);bindings.writeUInt32LE(49,i*24+12);}
  await put(0x200,bindings);await put(0x100,struct(32,32,{20:count},{24:p+0x200}));await ok('vkCreateDescriptorSetLayout',device,p+0x100,0,p);const set=await ptr(0);
  const handle=Buffer.alloc(8);handle.writeBigUInt64LE(set);await put(0x200,handle);
  const pushRange=Buffer.alloc(12);pushRange.writeUInt32LE(32);pushRange.writeUInt32LE(4,8);await put(0x220,pushRange);
  await put(0x100,struct(48,30,{20:1,32:push?1:0},{24:p+0x200,40:push?p+0x220:0}));await ok('vkCreatePipelineLayout',device,p+0x100,0,p);const layout=await ptr(0);
  return {setLayout:String(set),layout:String(layout)};
 }
 async function descriptor(layout,buffers){
  const count=buffers.length,size=Buffer.alloc(8);size.writeUInt32LE(7);size.writeUInt32LE(count,4);await put(0x200,size);
  await put(0x100,struct(40,33,{20:1,24:1},{32:p+0x200}));await ok('vkCreateDescriptorPool',device,p+0x100,0,p);const pool=await ptr(0),handle=Buffer.alloc(8);handle.writeBigUInt64LE(BigInt(layout.setLayout));await put(0x200,handle);
  await put(0x100,struct(40,34,{24:1},{16:pool,32:p+0x200}));await ok('vkAllocateDescriptorSets',device,p+0x100,p);const value=await ptr(0);
  for(const [binding,b] of buffers.entries()){
   const info=Buffer.alloc(24);info.writeBigUInt64LE(BigInt(b.buffer));info.writeBigUInt64LE(BigInt(b.bytes),16);await put(0x200,info);
   await put(0x100,struct(64,35,{24:binding,32:1,36:7},{16:value,48:p+0x200}));await call('vkUpdateDescriptorSets',device,1,p+0x100,0,0);
  }
  return {descriptor:String(value),descriptorPool:String(pool)};
 }
 try{
  await put(0,Buffer.alloc(0x10000));const instance=BigInt(display.displayResources.instance);
  await ok('vkEnumeratePhysicalDevices',instance,p+24,0);check((await rpc.read(p+24,4)).readUInt32LE()===1,'Physical device count differs');await ok('vkEnumeratePhysicalDevices',instance,p+24,p+32);physical=await ptr(32);
  await call('vkGetPhysicalDeviceMemoryProperties',physical,p+0x3000);properties=await rpc.read(p+0x3000,1024);
  const resident=await buffer((L.TABLE+L.MAX_SLOTS*L.STRIDE)*4,0x120,await fs.readFile(path.join(bundle,'resident.bin'))),residentLayout=await descriptorLayout(6,true),slotList=[];
  const input={buffer:menu.controls.buffer,bytes:menu.controls.bytes,mapped:menu.controls.mapped};
  const memory=require('./memory.cjs').create({budgetBytes:manifest.budgetBytes,baseBytes:require('./memory.cjs').BASE_BYTES});
  async function makeSlot(spec,bundle,appRoot){
   const dir=path.join(bundle,'slot-'+spec.slot),initialState=await buffer(spec.slot===0?L.STATE_BYTES:spec.initialStateBytes,0x20,await fs.readFile(path.join(dir,'initial-state.bin'))),initialPrivate=await buffer(spec.initialPrivateBytes,0x20,await fs.readFile(path.join(dir,'initial-private.bin')),spec.privateBytes>32*1048576);
   const controls=await buffer(L.STATE_BYTES,0x20,await fs.readFile(path.join(dir,'initial-state.bin'))),controlLayout=await descriptorLayout(1),controlDescriptor=await descriptor(controlLayout,[controls]);
   Object.assign(controls,controlLayout,controlDescriptor);
   const app=spec.slot===0?await C.readApp(menuDirectory):await C.readApp(appRoot);
   check(app.fingerprint===spec.fingerprint,'Resident app changed during preparation');
   const appDirectory=path.join(directory,'resident-app-'+spec.id+'-'+spec.fingerprint.slice(0,16));await fs.mkdir(appDirectory,{recursive:true});
   for(const name of ['compatibility-module.json','vulkan-display.json'])await fs.copyFile(path.join(directory,name),path.join(appDirectory,name));
   const gpu=await prepareApp(rpc,{...menu,layout:controls.layout,controls},app,appDirectory);
   check(gpu.ready&&gpu.scratchRestored&&gpu.descriptor,'Resident app pipelines incomplete');
   const privateStorage=gpu.privateStorage||await buffer(4);
   if(gpu.privateStorage)await require('../../../core/services/owned-buffer.cjs').initialize(rpc,privateStorage.mapped,privateStorage.bytes,await fs.readFile(path.join(dir,'initial-private.bin')),app.manifest.memory);
   const routingDescriptor=await descriptor(residentLayout,[resident,controls,privateStorage,initialState,initialPrivate,input]);
   return {...spec,controls,privateStorage,initialState,initialPrivate,layout:gpu.layout,descriptor:gpu.descriptor,graphics:gpu.pipeline,
    compute:{main:gpu.computePipeline,begin:gpu.computePipelines.computeBegin?.pipeline,after:gpu.computePipelines.computeAfter?.pipeline},residentDescriptor:routingDescriptor.descriptor,
    residentDescriptorPool:routingDescriptor.descriptorPool,gpu,app,appDirectory};
  }
  for(const spec of manifest.slots){
   const slot=await makeSlot(spec,bundle,supported[spec.slot-1]);slotList.push(slot);if(spec.slot!==0)memory.retain(slot.app,slot);
   report.slots=slotList.map(s=>({id:s.id,slot:s.slot,controls:s.controls,privateStorage:s.privateStorage}));report.memory=memory.status();await save();
  }
  const pipelines={};
  for(const name of ['router','slot-reset','slot-input','slot-post','service-control']){
   const bytes=await fs.readFile(path.join(bundle,'shaders',name+'.comp.spv'));check(bytes.length<=menu.shaderStaging.bytes,'Resident shader exceeds staging capacity');await rpc.write(menu.shaderStaging.address,bytes);
   await put(0x100,struct(40,16,{}, {24:bytes.length,32:menu.shaderStaging.address}));await ok('vkCreateShaderModule',device,p+0x100,0,p);const shader=await ptr(0);await put(0x200,Buffer.from('main\0'));
   const info=struct(96,29,{88:0xffffffff},{72:residentLayout.layout});struct(48,18,{20:32},{24:shader,32:p+0x200}).copy(info,24);await put(0x100,info);
   await ok('vkCreateComputePipelines',device,0,1,p+0x100,0,p);pipelines[name]=String(await ptr(0));
  }
  const graph={...resident,...residentLayout,pipelines};
  let commands=await record({call,ok,put,ptr,p,device,pool:BigInt(menu.resources.pool),images:menu.resources.images.map(BigInt),views:menu.resources.views.map(BigInt),previewBuffer:BigInt(menu.capture.buffer),capture:process.env.IZ12_GPU_PREVIEW==='1',resident:graph,slots:slotList});
  const audio=await require('./audio-lifetime.cjs').create({rpc,slots:slotList,root});
  report.resident=graph;report.commands=commands;report.audio=audio.status();report.ready=true;report.completed=new Date().toISOString();await save();
  const session={commands,report,slots:slotList,controls:resident,audio,memory,menu:slotList[0],
   async admit(app){
    const classification=source.candidate(app,{}, {finiteAudio});check(classification.compatible,classification.userReason);
    const admission=memory.admit(app);
    if(!admission.cached){check(!app.manifest.hostRuntime,'Preload modules that own audio into the RAM set before starting the session.');check(audio.status().mode==='graphics-only','Add applications before starting finite audio; its queue cannot pause to prepare another module.');}
    return admission;
   },
   async plan(apps){for(const app of apps)await session.admit(app);return memory.plan(apps);},
   async refresh(nextCatalog,{workerStopped=false}={}){
    check(workerStopped,'Resident RAM publication requires a joined render worker');
    const header=await rpc.read(resident.mapped,(L.TABLE+L.MAX_SLOTS*L.STRIDE)*4);check(header.readUInt32LE(L.H.active*4)===0,'Resident RAM publication requires the menu');
    const apps=[];for(const entry of nextCatalog.apps){const app=await C.readApp(entry.root);await session.admit(app);apps.push(app);}
    check(apps.length===memory.status().apps.length+apps.filter(app=>!memory.admit(app).cached).length,'Resident apps cannot be removed during this session');
    if(apps.length===slotList.length-1&&apps.every((app,i)=>app.fingerprint===slotList[i+1].fingerprint))return false;
    memory.plan(apps);
    const refreshScratch=await rpc.read(p,0x10000);try{
    const revision=(report.ramRevisions||0)+1,nextBundle=path.join(directory,'resident-bundle-'+revision);
    const next=await source.prepare(nextBundle,apps.map(app=>app.root),{budgetBytes:manifest.budgetBytes,qualifyFiniteAudio:finiteAudio,shell});
    const nextSlots=[slotList[0]];
    for(const spec of next.slots.slice(1)){
     const app=apps[spec.slot-1];let slot=memory.get(app);
     if(!slot){slot=await makeSlot(spec,nextBundle,app.root);memory.retain(app,slot);}
     nextSlots.push({...slot,...spec});
    }
    const menuInitial=await fs.readFile(path.join(nextBundle,'slot-0/initial-state.bin'));
    await require('../../../core/services/owned-buffer.cjs').initialize(rpc,slotList[0].initialState.mapped,L.STATE_BYTES,menuInitial);
    await require('../../../core/services/owned-buffer.cjs').initialize(rpc,slotList[0].controls.mapped,L.STATE_BYTES,menuInitial);
    const table=await fs.readFile(path.join(nextBundle,'resident.bin'));
    for(const field of [L.H.frames,L.H.inputs,L.H.transitions,L.H.faults])table.writeUInt32LE(header.readUInt32LE(field*4),field*4);
    table.writeUInt32LE((header.readUInt32LE(L.H.generation*4)+1)>>>0,L.H.generation*4);
    for(const spec of next.slots.slice(1)){
     const old=slotList.find(slot=>slot.fingerprint===spec.fingerprint);if(!old)continue;
     for(const field of [L.S.launches,L.S.frames,L.S.tickLow,L.S.tickHigh])table.writeUInt32LE(header.readUInt32LE(L.word(old.slot,field)*4),L.word(spec.slot,field)*4);
    }
    await rpc.write(resident.mapped,table);
    commands=await record({call,ok,put,ptr,p,device,pool:BigInt(menu.resources.pool),images:menu.resources.images.map(BigInt),views:menu.resources.views.map(BigInt),previewBuffer:BigInt(menu.capture.buffer),capture:process.env.IZ12_GPU_PREVIEW==='1',resident:graph,slots:nextSlots});
    slotList.splice(0,slotList.length,...nextSlots);manifest=next;session.commands=commands;
    report.commands=commands;report.bundle=next;report.ramRevisions=revision;report.memory=memory.status();report.slots=slotList.map(s=>({id:s.id,slot:s.slot,controls:s.controls,privateStorage:s.privateStorage}));await save();return true;
    }finally{await put(0,refreshScratch);const restored=(await rpc.read(p,refreshScratch.length)).equals(refreshScratch);report.scratchRestored=restored;await save();if(!restored){const error=Error('Resident RAM publication failed to restore scratch');error.restartRequired=true;throw error;}}
   },async observe(){
   const bytes=await rpc.read(resident.mapped,128),get=i=>bytes.readUInt32LE(i*4);check(get(0)===L.MAGIC&&get(1)===L.VERSION,'Resident control ABI changed');
   const slot=get(L.H.active);check(slot<slotList.length,'Resident active slot is invalid');
   return {slot,id:slotList[slot].id,frames:get(L.H.frames),transitions:get(L.H.transitions),faults:get(L.H.faults),generation:get(L.H.generation),audioProviderReady:audio.status().ready,audio:audio.status(),memory:memory.status()};
  }};return session;
 }catch(error){report.error=error.message;await save();throw error;}
 finally{await put(0,saved);report.scratchRestored=(await rpc.read(p,saved.length)).equals(saved);await save();check(report.scratchRestored,'Resident preparation failed to restore scratch');}
}
module.exports={prepare};
