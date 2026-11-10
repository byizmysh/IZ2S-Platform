// GPU-only IZ launcher mock, using the qualified cube display and frame worker.
const fs=require('node:fs/promises'),path=require('node:path');
const {journal}=require('../../../core/services/journal.cjs');
const {create}=require('../runtime/native-six.cjs');
const check=(v,m)=>{if(!v)throw Error(m);};
const struct=(size,type,words={},pointers={})=>{const b=Buffer.alloc(size);b.writeUInt32LE(type);for(const[a,v]of Object.entries(words))b.writeUInt32LE(v,+a);for(const[a,v]of Object.entries(pointers))b.writeBigUInt64LE(BigInt(v),+a);return b;};
async function main(directory,attempt=''){
 check(!attempt||/^[a-z0-9-]+$/.test(attempt),'Invalid attempt label');const stem='launcher'+(attempt?'-'+attempt:'');
 directory=path.resolve(directory);const rpc=require(path.join(directory,'shared/rpc.cjs'));
 const previous=JSON.parse(await fs.readFile(path.join(directory,'vulkan-display.json'))),module=JSON.parse(await fs.readFile(path.join(directory,'compatibility-module.json')));
 check(previous.session===rpc.a.session&&previous.resourcesRetained,'Wrong session');
 for(const name of ['cube.json','shader-loop.json','interactive-loop.json','touch-loop.json']){
  const active=await fs.readFile(path.join(directory,name),'utf8').then(JSON.parse,error=>{if(error.code==='ENOENT')return null;throw error;});
  if(active)check(active.session===rpc.a.session&&active.completed&&active.scratchRestored!==false,'Stop previous graphics work before preparing launcher: '+name);
 }
 for(const name of await fs.readdir(directory))if(/^launcher(?:-[a-z0-9-]+)?-loop\.json$/.test(name)||name==='launcher-loop.json'){const loop=JSON.parse(await fs.readFile(path.join(directory,name)));check(loop.completed,'Stop active launcher loop before preparing another: '+name);}
 const r=previous.displayResources,device=BigInt(r.device),queue=BigInt(r.queue),swapchain=BigInt(r.swapchain);
 const file=path.join(directory,stem+'.json');await fs.writeFile(file,'{}',{flag:'wx'});
 const catalogTools=require('../../../core/services/app-catalog.cjs'),catalog=await catalogTools.scan(process.env.HB_APPS_DIR||catalogTools.ROOT);
 const report={purpose:'IZ launcher GPU app packages from the PC; SD loading is not connected',runtimeABI:1,titleCount:catalog.apps.length,catalog:catalogTools.summary(catalog),session:rpc.a.session,started:new Date().toISOString(),resourcesRetained:true,frames:1,functions:{},commands:[]};
 const save=()=>fs.writeFile(file,JSON.stringify(report,null,2)),tracked=journal(report,save),fn=report.functions;
 const atlas=await require('./font-data.cjs').load();check(atlas.length===1024*2048,'Unexpected text atlas');const storageBytes=8*1024*1024;
 const catalogBytes=await catalogTools.raster(catalog,directory,stem+'-catalog');
 const initialState=()=>{const state=Buffer.alloc(1024);state.writeUInt32LE(report.titleCount,112);state.writeUInt32LE(report.titleCount,1020);state.writeFloatLE(1,960);return state;};
 // Validate all stages before native allocation. The compute shader can grow
 // independently of graphics stages when stream controls are extended.
 const shaderPlan=await require('./shader-staging.cjs').load(path.join(__dirname,'shaders'));
 const worker=await create(rpc,directory);
 async function resolve(name){if(fn[name])return fn[name];const s=module.symbols.find(s=>s.name===name&&s.section&&s.type===2);check(s,'Missing '+name);const a=module.base+s.offset,b=await rpc.read(a,32);await rpc.request({command:'verify-function',address:a,hex:b.toString('hex')});return fn[name]=a;}
 async function call(name,...args){const a=await resolve(name);return tracked(name,args,()=>args.length>4?worker.call(a,args):rpc.callAddress(a,args).then(x=>x.result));}
 const ok=async(name,...args)=>{const n=Number(BigInt.asIntN(32,await call(name,...args)));check(n===0,name+': '+n);};
 const p=rpc.a.scratch+0x10000,saved=await rpc.read(p,0x10000),put=(o,b)=>rpc.write(p+o,b),ptr=async o=>(await rpc.read(p+o,8)).readBigUInt64LE();
 try{
  await put(0,Buffer.alloc(0x10000));const shaders=[];
  // Shader growth uses an owned staging allocation, beyond the old 32 KiB slot.
  check((await rpc.call(5732112,[p,65536,shaderPlan.bytes])).result===0n,'Shader staging allocation failed');
  const shaderStaging=Number(await ptr(0));check(Number.isSafeInteger(shaderStaging)&&shaderStaging>0&&shaderStaging%65536===0,'Invalid shader staging allocation');
  report.shaderStaging={address:shaderStaging,bytes:shaderPlan.bytes,stageBytes:Object.fromEntries(Object.entries(shaderPlan.shaders).map(([stage,bytes])=>[stage,bytes.length]))};await save();
  for(const stage of ['vert','frag']){const bytes=shaderPlan.shaders[stage];check(bytes.length<=shaderPlan.bytes,'Shader exceeds staging space');await rpc.write(shaderStaging,bytes);await put(0x100,struct(40,16,{}, {24:bytes.length,32:shaderStaging}));await ok('vkCreateShaderModule',device,p+0x100,0,p);shaders.push(await ptr(0));}
  const binding=Buffer.alloc(24);binding.writeUInt32LE(7,4);binding.writeUInt32LE(1,8);binding.writeUInt32LE(49,12);await put(0x200,binding);
  await put(0x100,struct(32,32,{20:1},{24:p+0x200}));await ok('vkCreateDescriptorSetLayout',device,p+0x100,0,p);const setLayout=await ptr(0);
  await put(0x100,struct(48,30,{20:1},{24:p}));await ok('vkCreatePipelineLayout',device,p+0x100,0,p+8);const layout=await ptr(8);
  await ok('vkEnumeratePhysicalDevices',BigInt(r.instance),p+24,0);check((await rpc.read(p+24,4)).readUInt32LE()===1,'Physical device count');await ok('vkEnumeratePhysicalDevices',BigInt(r.instance),p+24,p+32);const physical=await ptr(32);
  await call('vkGetPhysicalDeviceMemoryProperties',physical,p+0x7000);const properties=await rpc.read(p+0x7000,520);
  await put(0x100,struct(56,12,{32:0x20},{24:storageBytes}));await ok('vkCreateBuffer',device,p+0x100,0,p);const buffer=await ptr(0);
  await call('vkGetBufferMemoryRequirements',device,buffer,p+0x200);const requirements=await rpc.read(p+0x200,24),size=requirements.readBigUInt64LE(),bits=requirements.readUInt32LE(16);
  let memoryType=-1;for(let i=0;i<properties.readUInt32LE();i++)if((bits&(1<<i))&&((properties.readUInt32LE(4+i*8)&6)===6)){memoryType=i;break;}check(memoryType>=0&&size<=33554432n,'Coherent storage memory unavailable');
  await put(0x100,struct(32,5,{24:memoryType},{16:size}));await ok('vkAllocateMemory',device,p+0x100,0,p);const memory=await ptr(0);await ok('vkBindBufferMemory',device,buffer,memory,0);
  await put(0x100,struct(48,1000271000,{}, {24:memory,40:size}));await ok('vkMapMemory2',device,p+0x100,p);const mapped=Number(await ptr(0));check(Number.isSafeInteger(mapped),'Invalid mapped address');
  report.upload=await require('../runtime/storage.cjs').initialize(rpc,mapped,storageBytes,[{offset:0,bytes:initialState()},{offset:1024,bytes:atlas},{offset:1024+atlas.length,bytes:catalogBytes}]);
  const previewBytes=1920*1080*4;
  await put(0x100,struct(56,12,{32:2},{24:previewBytes}));await ok('vkCreateBuffer',device,p+0x100,0,p);const previewBuffer=await ptr(0);
  await call('vkGetBufferMemoryRequirements',device,previewBuffer,p+0x200);const previewReq=await rpc.read(p+0x200,24),previewSize=previewReq.readBigUInt64LE(),previewBits=previewReq.readUInt32LE(16);
  let previewType=-1;for(let j=0;j<properties.readUInt32LE();j++)if((previewBits&(1<<j))&&((properties.readUInt32LE(4+j*8)&6)===6)){previewType=j;break;}check(previewType>=0&&previewSize<=33554432n,'Preview memory unavailable');
  await put(0x100,struct(32,5,{24:previewType},{16:previewSize}));await ok('vkAllocateMemory',device,p+0x100,0,p);const previewMemory=await ptr(0);await ok('vkBindBufferMemory',device,previewBuffer,previewMemory,0);
  await put(0x100,struct(48,1000271000,{}, {24:previewMemory,40:previewSize}));await ok('vkMapMemory2',device,p+0x100,p);const previewMapped=Number(await ptr(0));
  check(Number.isSafeInteger(previewMapped)&&previewMapped>0,'Invalid preview address');report.capture={buffer:String(previewBuffer),memory:String(previewMemory),mapped:previewMapped,bytes:previewBytes,width:1920,height:1080};
  const poolSize=Buffer.alloc(8);poolSize.writeUInt32LE(7);poolSize.writeUInt32LE(1,4);await put(0x200,poolSize);
  await put(0x100,struct(40,33,{20:1,24:1},{32:p+0x200}));await ok('vkCreateDescriptorPool',device,p+0x100,0,p);const descriptorPool=await ptr(0);
  const sl=Buffer.alloc(8);sl.writeBigUInt64LE(setLayout);await put(0x200,sl);
  await put(0x100,struct(40,34,{24:1},{16:descriptorPool,32:p+0x200}));await ok('vkAllocateDescriptorSets',device,p+0x100,p);const descriptor=await ptr(0);
  const bufferInfo=Buffer.alloc(24);bufferInfo.writeBigUInt64LE(buffer);bufferInfo.writeBigUInt64LE(BigInt(storageBytes),16);await put(0x200,bufferInfo);
  await put(0x100,struct(64,35,{32:1,36:7},{16:descriptor,48:p+0x200}));await call('vkUpdateDescriptorSets',device,1,p+0x100,0,0);
  report.controls={buffer:String(buffer),memory:String(memory),mapped,setLayout:String(setLayout),descriptorPool:String(descriptorPool),descriptor:String(descriptor),bytes:storageBytes};await save();

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
  console.log('Creating IZ launcher menu pipeline.');
  await ok('vkCreateGraphicsPipelines',device,0,1,p+0xe00,0,p);const pipeline=await ptr(0);report.pipeline=String(pipeline);report.layout=String(layout);report.shaders=shaders.map(String);await save();
  const computeBytes=shaderPlan.shaders.comp;check(computeBytes.length<=shaderPlan.bytes,'Compute shader exceeds staging space');await rpc.write(shaderStaging,computeBytes);await put(0x100,struct(40,16,{}, {24:computeBytes.length,32:shaderStaging}));await ok('vkCreateShaderModule',device,p+0x100,0,p);const computeShader=await ptr(0);
  await put(0x200,Buffer.from('main\0'));const computeInfo=struct(96,29,{88:0xffffffff},{72:layout});struct(48,18,{20:32},{24:computeShader,32:p+0x200}).copy(computeInfo,24);await put(0x100,computeInfo);
  await ok('vkCreateComputePipelines',device,0,1,p+0x100,0,p);const computePipeline=await ptr(0);report.computePipeline=String(computePipeline);report.computeShader=String(computeShader);await save();
  await ok('vkGetSwapchainImagesKHR',device,swapchain,p+24,0);const count=(await rpc.read(p+24,4)).readUInt32LE();check(count>0&&count<=8,'Image count');await ok('vkGetSwapchainImagesKHR',device,swapchain,p+24,p+0x1000);
  const images=[],views=[],semaphores=[];
  await put(0x100,struct(24,9));await ok('vkCreateSemaphore',device,p+0x100,0,p);const acquired=await ptr(0);
  for(let i=0;i<count;i++){const image=await ptr(0x1000+i*8);images.push(image);await put(0x200,struct(80,15,{32:1,36:37,56:1,64:1,72:1},{24:image}));await ok('vkCreateImageView',device,p+0x200,0,p);views.push(await ptr(0));await ok('vkCreateSemaphore',device,p+0x100,0,p);semaphores.push(await ptr(0));}
  await put(0x100,struct(24,39,{20:previous.queueFamily}));await ok('vkCreateCommandPool',device,p+0x100,0,p);const pool=await ptr(0);
  report.resources={device:String(device),queue:String(queue),swapchain:String(swapchain),images:images.map(String),views:views.map(String),semaphores:semaphores.map(String),acquired:String(acquired),pool:String(pool)};await save();
  // Compensate for the qualification dispatch so its presented frame stays black.
  const synthetic=initialState();synthetic.writeFloatLE(1+1/15,960);synthetic.writeFloatLE(8,64);synthetic.writeUInt32LE(1,32);await rpc.write(mapped,synthetic);
  for(let frame=0;frame<1;frame++){
   const commands=[];
   for(let i=0;i<count;i++){
    await put(0x100,struct(32,40,{28:1},{16:pool}));await ok('vkAllocateCommandBuffers',device,p+0x100,p);const command=await ptr(0);
    await put(0x200,struct(32,42));await ok('vkBeginCommandBuffer',command,p+0x200);
    async function barrier(oldLayout,newLayout,srcStage,srcAccess,dstStage,dstAccess){await put(0x300,struct(96,1000314002,{48:oldLayout,52:newLayout,56:0xffffffff,60:0xffffffff,72:1,80:1,88:1},{16:srcStage,24:srcAccess,32:dstStage,40:dstAccess,64:images[i]}));await put(0x400,struct(64,1000314003,{48:1},{56:p+0x300}));await call('vkCmdPipelineBarrier2',command,p+0x400);}
    async function memoryBarrier(srcStage,srcAccess,dstStage,dstAccess){await put(0x1200,struct(48,1000314000,{}, {16:srcStage,24:srcAccess,32:dstStage,40:dstAccess}));await put(0x1300,struct(64,1000314003,{20:1},{24:p+0x1200}));await call('vkCmdPipelineBarrier2',command,p+0x1300);}
    await memoryBarrier(0x4888,0x4060,0x800,0x60);
    const ds=Buffer.alloc(8);ds.writeBigUInt64LE(descriptor);await put(0x1180,ds);await put(0x1100,struct(64,1000545003,{16:49,36:1},{24:layout,40:p+0x1180}));
    await call('vkCmdBindDescriptorSets2',command,p+0x1100);await call('vkCmdBindPipeline',command,1,computePipeline);await call('vkCmdDispatch',command,1,1,1);
    await memoryBarrier(0x800,0x40,0x88,0x20);
    await barrier(0,2,0,0,0x400,0x100);
    const attachment=struct(72,1000044001,{24:2,44:1},{16:views[i]});[.012,.02,.045,1].forEach((v,c)=>attachment.writeFloatLE(v,52+c*4));await put(0x500,attachment);
    await put(0x600,struct(72,1000044000,{28:1920,32:1080,36:1,44:1},{48:p+0x500}));await call('vkCmdBeginRendering',command,p+0x600);
    await call('vkCmdBindPipeline',command,0,pipeline);await call('vkCmdDraw',command,3,1,0,frame);await call('vkCmdEndRendering',command);
    await barrier(2,6,0x400,0x100,0x1000,0x800);
    await put(0x1400,struct(72,1000337009,{32:1,44:1,60:1920,64:1080,68:1}));
    await put(0x1500,struct(56,1000337003,{24:6,40:1},{16:images[i],32:previewBuffer,48:p+0x1400}));await call('vkCmdCopyImageToBuffer2',command,p+0x1500);
    await memoryBarrier(0x1000,0x1000,0x4000,0x2000);
    await barrier(6,1000001002,0x1000,0x800,0,0);await memoryBarrier(0x800,0x40,0x4000,0x2000);await ok('vkEndCommandBuffer',command);commands.push(String(command));
   }
   report.commands.push(commands);await fs.appendFile(path.join(directory,stem+'-operations.jsonl'),report.operations.map(x=>JSON.stringify(x)).join('\n')+'\n');report.operations=[];await save();
   console.log('Recorded interactive command buffers for every swapchain image.');
   // Present one real shader frame before completing the command set.
   if(frame===0){
    await put(0x200,struct(56,1000060010,{48:1},{16:swapchain,24:1000000000n,32:acquired}));await ok('vkAcquireNextImage2KHR',device,p+0x200,p+16);const index=(await rpc.read(p+16,4)).readUInt32LE();check(index<count,'Image index');
    const handles=Buffer.alloc(24);[acquired,BigInt(commands[index]),semaphores[index]].forEach((v,i)=>handles.writeBigUInt64LE(v,i*8));await put(0x800,handles);
    const mask=Buffer.alloc(4);mask.writeUInt32LE(0x400);await put(0x900,mask);await put(0xa00,struct(72,4,{16:1,40:1,56:1},{24:p+0x800,32:p+0x900,48:p+0x808,64:p+0x810}));await ok('vkQueueSubmit',queue,1,p+0xa00,0);
    const sw=Buffer.alloc(8);sw.writeBigUInt64LE(swapchain);await put(0xb00,sw);await put(0xc00,struct(64,1000001001,{16:1,32:1},{24:p+0x810,40:p+0xb00,48:p+16}));await ok('vkQueuePresentKHR',queue,p+0xc00);await ok('vkQueueWaitIdle',queue);report.firstShaderFramePresented=true;if(process.env.IZ2S_CIV_CAPTURE==='1')await fs.writeFile(path.join(directory,stem+'-preview.rgba'),await rpc.read(previewMapped,previewBytes),{flag:'wx'});report.framePreviewCaptured=process.env.IZ2S_CIV_CAPTURE==='1';await save();const feedback=await rpc.read(mapped+64,32);report.computeCheck={angleX:feedback.readFloatLE(),palette:feedback.readUInt32LE(16),buttons:feedback.readUInt32LE(20),frames:feedback.readUInt32LE(24),modal:feedback.readUInt32LE(28)};check(Math.abs(report.computeCheck.angleX-(8+1/60))<.00001&&report.computeCheck.palette===0&&report.computeCheck.buttons===0&&report.computeCheck.frames===1&&report.computeCheck.modal===0,'Compute input feedback mismatch');await rpc.write(mapped,initialState());await save();console.log('IZ launcher presented; GPU navigation feedback verified.');
   }
  }
  for(const name of ['vkAcquireNextImage2KHR','vkQueueSubmit','vkQueuePresentKHR','vkQueueWaitIdle'])await resolve(name);
  report.ready=true;
 }catch(e){report.error=e.stack;console.error(e);process.exitCode=1;}
 finally{await put(0,saved);report.scratchRestored=(await rpc.read(p,saved.length)).equals(saved);report.completed=new Date().toISOString();await save();}
}
if(require.main===module)main(process.argv[2],process.argv[3]).catch(e=>{console.error(e);process.exitCode=1;});

module.exports = {main};
