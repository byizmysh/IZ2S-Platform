// Create independent shader pipelines/commands using the retained launcher surface.
const fs=require('node:fs/promises'),path=require('node:path');
const {create}=require('./native-six.cjs');
const {recordPresentation}=require('./gpu-presentation.cjs');
const check=(v,m)=>{if(!v)throw Error(m);};
const struct=(size,type,words={},pointers={})=>{const b=Buffer.alloc(size);b.writeUInt32LE(type);for(const[a,v]of Object.entries(words))b.writeUInt32LE(v,+a);for(const[a,v]of Object.entries(pointers))b.writeBigUInt64LE(BigInt(v),+a);return b;};
async function prepare(rpc,menu,app,directory){
 const computeWorkgroups=app.manifest.computeWorkgroups===undefined?[1,1,1]:app.manifest.computeWorkgroups;
 const module=JSON.parse(await fs.readFile(path.join(directory,'compatibility-module.json')));
 const file=path.join(directory,'app-'+app.id+'-'+app.fingerprint.slice(0,16)+'.json');
 await fs.writeFile(file,'{}',{flag:'wx'});
 const captureFrames=process.env.IZ12_GPU_PREVIEW==='1';
 const report={framePreview:{enabled:captureFrames,bytesPerFrame:captureFrames?1920*1080*4:0},session:rpc.a.session,id:app.id,fingerprint:app.fingerprint,started:new Date().toISOString(),resourcesRetained:true,commands:[],functions:{}};
 const save=()=>fs.writeFile(file,JSON.stringify(report,null,2));
 const worker=await create(rpc,directory),fn=report.functions;
 async function resolve(name){if(fn[name])return fn[name];const s=module.symbols.find(s=>s.name===name&&s.section&&s.type===2);check(s,'Missing '+name);const address=module.base+s.offset;await rpc.request({command:'verify-function',address,hex:(await rpc.read(address,32)).toString('hex')});return fn[name]=address;}
 async function call(name,...args){const address=await resolve(name);return args.length>4?worker.call(address,args):rpc.callAddress(address,args).then(r=>r.result);}
 const ok=async(name,...args)=>check(BigInt.asIntN(32,await call(name,...args))===0n,name+' failed');
 const p=rpc.a.scratch+0x10000,saved=await rpc.read(p,0x10000),put=(o,b)=>rpc.write(p+o,b),ptr=async o=>(await rpc.read(p+o,8)).readBigUInt64LE();
 const device=BigInt(menu.resources.device),queue=BigInt(menu.resources.queue),swapchain=BigInt(menu.resources.swapchain);
 let shaderStaging=menu.shaderStaging.address,stagingBytes=menu.shaderStaging.bytes,layout=BigInt(menu.layout),descriptor=BigInt(menu.controls.descriptor);
 const images=menu.resources.images.map(BigInt),views=menu.resources.views.map(BigInt),pool=BigInt(menu.resources.pool),previewBuffer=BigInt(menu.capture.buffer);
 try{
  await put(0,Buffer.alloc(0x10000));const shaders=[];
  const largest=Math.max(...['vertex','fragment','compute','computeBegin','computeAfter'].filter(s=>app.files[s]).map(s=>app.files[s].bytes.length));
  if(largest>stagingBytes){stagingBytes=Math.ceil(largest/65536)*65536;const allocation=await rpc.call(5732112,[rpc.output,65536,stagingBytes]);check(allocation.result===0n,'App shader staging allocation failed');shaderStaging=Number(allocation.output.readBigUInt64LE());check(Number.isSafeInteger(shaderStaging)&&shaderStaging>0&&shaderStaging%65536===0,'Invalid app shader staging');report.shaderStaging={address:shaderStaging,bytes:stagingBytes};}
  if(app.files.memory){
   const display=JSON.parse(await fs.readFile(path.join(directory,'vulkan-display.json')));
   const instance=BigInt(display.displayResources.instance);await ok('vkEnumeratePhysicalDevices',instance,p+24,0);check((await rpc.read(p+24,4)).readUInt32LE()===1,'Physical device count');await ok('vkEnumeratePhysicalDevices',instance,p+24,p+32);const physical=await ptr(32);
   await call('vkGetPhysicalDeviceMemoryProperties',physical,p+0x3000);const properties=await rpc.read(p+0x3000,1024),bytes=app.files.memory.allocationBytes;
   await put(0x100,struct(56,12,{32:0x20},{24:bytes}));await ok('vkCreateBuffer',device,p+0x100,0,p);const buffer=await ptr(0);
   await call('vkGetBufferMemoryRequirements',device,buffer,p+0x200);const requirements=await rpc.read(p+0x200,24),size=requirements.readBigUInt64LE(),bits=requirements.readUInt32LE(16);let type=-1;
   for(const required of [7,6]){for(let i=0;i<properties.readUInt32LE();i++)if((bits&(1<<i))&&((properties.readUInt32LE(4+i*8)&required)===required)){type=i;break;}if(type>=0)break;}
   check(type>=0&&require('./private-memory-profile.cjs').allocationFits(app.manifest.memory,size),'Private coherent storage unavailable');await put(0x100,struct(32,5,{24:type},{16:size}));await ok('vkAllocateMemory',device,p+0x100,0,p);const memory=await ptr(0);await ok('vkBindBufferMemory',device,buffer,memory,0);
   await put(0x100,struct(48,1000271000,{}, {24:memory,40:size}));await ok('vkMapMemory2',device,p+0x100,p);const mapped=Number(await ptr(0));check(Number.isSafeInteger(mapped)&&mapped>0,'Invalid private mapping');
   const bindings=Buffer.alloc(48);for(let i=0;i<2;i++){bindings.writeUInt32LE(i,i*24);bindings.writeUInt32LE(7,i*24+4);bindings.writeUInt32LE(1,i*24+8);bindings.writeUInt32LE(49,i*24+12);}await put(0x200,bindings);
   await put(0x100,struct(32,32,{20:2},{24:p+0x200}));await ok('vkCreateDescriptorSetLayout',device,p+0x100,0,p);const set=await ptr(0),handle=Buffer.alloc(8);handle.writeBigUInt64LE(set);await put(0x200,handle);
   await put(0x100,struct(48,30,{20:1},{24:p+0x200}));await ok('vkCreatePipelineLayout',device,p+0x100,0,p);layout=await ptr(0);
   const poolSize=Buffer.alloc(8);poolSize.writeUInt32LE(7);poolSize.writeUInt32LE(2,4);await put(0x200,poolSize);await put(0x100,struct(40,33,{20:1,24:1},{32:p+0x200}));await ok('vkCreateDescriptorPool',device,p+0x100,0,p);const poolHandle=await ptr(0);
   await put(0x200,handle);await put(0x100,struct(40,34,{24:1},{16:poolHandle,32:p+0x200}));await ok('vkAllocateDescriptorSets',device,p+0x100,p);descriptor=await ptr(0);
   for(const [binding,bufferHandle,length]of [[0,menu.controls.buffer,menu.controls.bytes],[1,String(buffer),bytes]]){const info=Buffer.alloc(24);info.writeBigUInt64LE(BigInt(bufferHandle));info.writeBigUInt64LE(BigInt(length),16);await put(0x200,info);await put(0x100,struct(64,35,{24:binding,32:1,36:7},{16:descriptor,48:p+0x200}));await call('vkUpdateDescriptorSets',device,1,p+0x100,0,0);}
   report.privateStorage={buffer:String(buffer),memory:String(memory),mapped,bytes,allocationBytes:Number(size),profile:app.manifest.memory.profile||'standard-32',memoryFlags:properties.readUInt32LE(4+type*8),descriptor:String(descriptor),setLayout:String(set),descriptorPool:String(poolHandle)};await save();
  }
  for(const stage of ['vertex','fragment']){const bytes=app.files[stage].bytes;check(bytes.length<=stagingBytes,'Shader staging capacity exceeded');await rpc.write(shaderStaging,bytes);await put(0x100,struct(40,16,{}, {24:bytes.length,32:shaderStaging}));await ok('vkCreateShaderModule',device,p+0x100,0,p);shaders.push(await ptr(0));}
  await put(0x200,Buffer.from('main\0'));
  await put(0x300,Buffer.concat(shaders.map((shader,i)=>struct(48,18,{20:i?16:1},{24:shader,32:p+0x200}))));
  await put(0x400,struct(48,19));await put(0x500,struct(32,20,{20:3}));
  const viewport=Buffer.alloc(24);[0,0,1920,1080,0,1].forEach((v,i)=>viewport.writeFloatLE(v,i*4));await put(0x600,viewport);
  const scissor=Buffer.alloc(16);scissor.writeUInt32LE(1920,8);scissor.writeUInt32LE(1080,12);await put(0x620,scissor);
  await put(0x700,struct(48,22,{20:1,32:1},{24:p+0x600,40:p+0x620}));
  const raster=struct(64,23,{32:0,36:0});raster.writeFloatLE(1,56);await put(0x800,raster);
  await put(0x900,struct(48,24,{20:1}));
  const blend=Buffer.alloc(32);blend.writeUInt32LE(15,28);await put(0xa00,blend);
  await put(0xb00,struct(56,26,{28:1},{32:p+0xa00}));
  const format=Buffer.alloc(4);format.writeUInt32LE(37);await put(0xc00,format);
  await put(0xd00,struct(40,1000044002,{20:1},{24:p+0xc00}));
  await put(0xe00,struct(144,28,{20:2,136:0xffffffff},{8:p+0xd00,24:p+0x300,32:p+0x400,40:p+0x500,56:p+0x700,64:p+0x800,72:p+0x900,88:p+0xb00,104:layout}));
  console.log('Creating app pipelines: '+app.name);
  await ok('vkCreateGraphicsPipelines',device,0,1,p+0xe00,0,p);const pipeline=await ptr(0);report.pipeline=String(pipeline);report.layout=String(layout);report.shaders=shaders.map(String);await save();
  const computePipelines={};report.computePipelines={};
  const padShader=await require('./pad-normalize.cjs').shader(directory);
  for(const stage of ['padNormalize',...['compute','computeBegin','computeAfter'].filter(s=>app.files[s])]){
   const bytes=stage==='padNormalize'?padShader:app.files[stage].bytes;check(bytes.length<=stagingBytes,'Compute shader exceeds staging space');await rpc.write(shaderStaging,bytes);await put(0x100,struct(40,16,{}, {24:bytes.length,32:shaderStaging}));await ok('vkCreateShaderModule',device,p+0x100,0,p);const shader=await ptr(0);
   await put(0x200,Buffer.from('main\0'));const info=struct(96,29,{88:0xffffffff},{72:layout});struct(48,18,{20:32},{24:shader,32:p+0x200}).copy(info,24);await put(0x100,info);
   await ok('vkCreateComputePipelines',device,0,1,p+0x100,0,p);computePipelines[stage]=await ptr(0);report.computePipelines[stage]={pipeline:String(computePipelines[stage]),shader:String(shader)};
  }
  const computePipeline=computePipelines.compute;report.computePipeline=String(computePipeline);report.computeShader=report.computePipelines.compute.shader;await save();

  async function bindSet(command){const ds=Buffer.alloc(8);ds.writeBigUInt64LE(descriptor);await put(0x1180,ds);await put(0x1100,struct(64,1000545003,{16:49,36:1},{24:layout,40:p+0x1180}));await call('vkCmdBindDescriptorSets2',command,p+0x1100);}
  async function computeBarrier(command){await put(0x1200,struct(48,1000314000,{}, {16:0x800,24:0x40,32:0x800,40:0x60}));await put(0x1300,struct(64,1000314003,{20:1},{24:p+0x1200}));await call('vkCmdPipelineBarrier2',command,p+0x1300);}
  let secondary=0n;
  if(app.manifest.computeSequence){
   // Record the reusable pair once. SIMULTANEOUS_USE permits repeated uses in
   // each primary; no RPC or CPU game loop is needed between GPU passes.
   await put(0x100,struct(32,40,{24:1,28:1},{16:pool}));await ok('vkAllocateCommandBuffers',device,p+0x100,p);secondary=await ptr(0);
   await put(0x300,struct(56,41));await put(0x200,struct(32,42,{16:4},{24:p+0x300}));await ok('vkBeginCommandBuffer',secondary,p+0x200);await bindSet(secondary);
   for(const stage of ['compute','computeAfter']){const groups=stage==='compute'?computeWorkgroups:[1,1,1];await call('vkCmdBindPipeline',secondary,1,computePipelines[stage]);await call('vkCmdDispatch',secondary,...groups);await computeBarrier(secondary);}
   await ok('vkEndCommandBuffer',secondary);report.computeSequence={secondary:String(secondary),repeat:app.manifest.computeSequence.repeat};await save();
  }

  const commands=[];const frame=0;
  for(let i=0;i<images.length;i++){
    await put(0x100,struct(32,40,{28:1},{16:pool}));await ok('vkAllocateCommandBuffers',device,p+0x100,p);const command=await ptr(0);
    await put(0x200,struct(32,42));await ok('vkBeginCommandBuffer',command,p+0x200);
    async function barrier(oldLayout,newLayout,srcStage,srcAccess,dstStage,dstAccess){await put(0x300,struct(96,1000314002,{48:oldLayout,52:newLayout,56:0xffffffff,60:0xffffffff,72:1,80:1,88:1},{16:srcStage,24:srcAccess,32:dstStage,40:dstAccess,64:images[i]}));await put(0x400,struct(64,1000314003,{48:1},{56:p+0x300}));await call('vkCmdPipelineBarrier2',command,p+0x400);}
    async function memoryBarrier(srcStage,srcAccess,dstStage,dstAccess){await put(0x1200,struct(48,1000314000,{}, {16:srcStage,24:srcAccess,32:dstStage,40:dstAccess}));await put(0x1300,struct(64,1000314003,{20:1},{24:p+0x1200}));await call('vkCmdPipelineBarrier2',command,p+0x1300);}
    await memoryBarrier(0x4888,0x4060,0x800,0x60);
    await bindSet(command);
    await call('vkCmdBindPipeline',command,1,computePipelines.padNormalize);await call('vkCmdDispatch',command,1,1,1);await computeBarrier(command);
    if(secondary){
     await call('vkCmdBindPipeline',command,1,computePipelines.computeBegin);await call('vkCmdDispatch',command,1,1,1);await computeBarrier(command);
     const count=app.manifest.computeSequence.repeat,handles=Buffer.alloc(count*8);for(let j=0;j<count;j++)handles.writeBigUInt64LE(secondary,j*8);await put(0x5000,handles);await call('vkCmdExecuteCommands',command,count,p+0x5000);
     // Secondary execution invalidates primary bindings.
     await bindSet(command);
    }else{await call('vkCmdBindPipeline',command,1,computePipeline);await call('vkCmdDispatch',command,...computeWorkgroups);}
    await memoryBarrier(0x800,0x40,0x88,0x20);
    await barrier(0,2,0,0,0x400,0x100);
    const attachment=struct(72,1000044001,{24:2,44:1},{16:views[i]});[.012,.02,.045,1].forEach((v,c)=>attachment.writeFloatLE(v,52+c*4));await put(0x500,attachment);
    await put(0x600,struct(72,1000044000,{28:1920,32:1080,36:1,44:1},{48:p+0x500}));await call('vkCmdBeginRendering',command,p+0x600);
    await call('vkCmdBindPipeline',command,0,pipeline);await call('vkCmdDraw',command,3,1,0,frame);await call('vkCmdEndRendering',command);
    await recordPresentation({capture:captureFrames,command,image:images[i],previewBuffer,p,put,call,memoryBarrier,barrier});
    await memoryBarrier(0x800,0x40,0x4000,0x2000);await ok('vkEndCommandBuffer',command);commands.push(String(command));
   }

   report.commands=[commands];report.descriptor=String(descriptor);report.ready=true;
 }catch(error){report.error=error.stack;throw error;}
 finally{await put(0,saved);report.scratchRestored=(await rpc.read(p,saved.length)).equals(saved);report.completed=new Date().toISOString();await save();check(report.scratchRestored,'App preparation failed to restore scratch');}
 return report;
}
module.exports={prepare};
