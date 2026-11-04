// Direct Vulkan GPU transfer with independent resources and byte-for-byte
// verification. LP64 layouts/constants follow Khronos Vulkan-Headers.
const fs=require('node:fs/promises'),path=require('node:path'),crypto=require('node:crypto');
const {journal}=require('./journal.cjs');
const {parseCompositionRequest,readSupportedCompositeAlpha,selectCompositeAlpha}=require('./vulkan-composition.cjs');
const check=(ok,msg)=>{if(!ok)throw Error(msg);};
const struct=(size,type,words={},pointers={})=>{
 const b=Buffer.alloc(size);b.writeUInt32LE(type,0);
 for(const [at,value] of Object.entries(words))b.writeUInt32LE(value,Number(at));
 for(const [at,value] of Object.entries(pointers))b.writeBigUInt64LE(BigInt(value),Number(at));return b;
};
async function main(directory,mode='copy',attempt){
 if(attempt)check(/^[a-z0-9-]+$/.test(attempt),'Invalid attempt label');
 check(mode!=='present','Presentation prototype disabled: no visible image, then disconnect during vkDestroySwapchainKHR. Inspect presentation completion and layer composition before retrying.');
 check(['copy','render','display','inspect'].includes(mode),'Unknown experiment');const present=mode==='display',render=mode==='render'||present;
 const compositionRequest=parseCompositionRequest(process.env.IZ2S_VULKAN_COMPOSITION);
 const wsi=present||mode==='inspect';
 const byteCount=render?1920*1080*4:65536;
 directory=path.resolve(directory);const rpc=require(path.join(directory,'shared/rpc.cjs'));
 const module=JSON.parse(await fs.readFile(path.join(directory,'compatibility-module.json')));
 const report={session:rpc.a.session,started:new Date().toISOString(),bytes:byteCount,api:'Vulkan 1.4',displayTouched:false,mode};
 const file=path.join(directory,'vulkan-'+mode+(attempt?'-'+attempt:'')+'.json');const lock=await fs.open(file,'wx',0o600);await lock.close();
 const save=()=>fs.writeFile(file,JSON.stringify(report,null,2),{mode:0o600});
 const tracked=journal(report,save),functions={};
 async function resolve(name){
  if(functions[name])return functions[name];
  const s=module.symbols.find(s=>s.name===name&&s.section&&s.type===2);check(s,'Missing '+name);
  const address=module.base+s.offset,bytes=await rpc.read(address,Math.min(32,s.size));
  await rpc.request({command:'verify-function',address,hex:bytes.toString('hex')});return functions[name]=address;
 }
 const call=async(name,...args)=>{const address=await resolve(name);return tracked(name,args,async()=>(await rpc.callAddress(address,args)).result);};
 const ok=async(name,...args)=>{const r=Number(BigInt.asIntN(32,await call(name,...args)));check(r===0,`${name}: VkResult ${r}`);};
 const p=rpc.a.scratch+0x10000,saved=await rpc.read(p,0x10000);
 const put=(offset,b)=>rpc.write(p+offset,b);
 const pointer=async offset=>{const v=(await rpc.read(p+offset,8)).readBigUInt64LE();check(v>0n,'Null output handle');return v;};
 let instance=0n,device=0n,pool=0n,image=0n,imageView=0n,submitted=false,finished=false;
 let layer=0,display=0n,layerOpened=false,surface=0n,swapchain=0n,acquireSemaphore=0n,presentSemaphore=0n,imageIndex=0;
 let retainDisplayResources=false;
 const sdk=require('../console/abi/sdk-functions.json');
 const sdkCall=(name,...args)=>tracked('sdk.'+name,args,async()=>(await rpc.call(sdk[name].offset,args)).result);
 const buffers=[],memories=[];
 try{
  await put(0,Buffer.alloc(0x10000));
  if(present){
   for(const n of ['allocate','free','memset']){const b=Buffer.from(sdk[n].bytes,'hex');check((await rpc.read(rpc.a.sdk+sdk[n].offset,b.length)).equals(b),'SDK differs');}
   check(await sdkCall('allocate',p,4096,0x2000)===0n,'Layer allocation failed');layer=Number(await pointer(0));
   check(Number.isSafeInteger(layer)&&layer%4096===0,'Invalid layer allocation');await sdkCall('memset',layer,0,0x2000);
   check(await call('_ZN2nn2vi18OpenDefaultDisplayEPPNS0_7DisplayE',p+8)===0n,'Display reference failed');display=await pointer(8);
   await call('_ZN2nn2vi5LayerC1Ev',layer);await call('_ZN2nn2vi21LayerCreationSettingsC1Eii',layer+0x1000,1920,1080);
   await rpc.write(layer+0x1100,Buffer.from('default\0'));
   check(await call('_ZN2nn2vi5Layer4OpenEPKNS0_21LayerCreationSettingsEPNS0_7DisplayEPKc',layer,layer+0x1000,display,layer+0x1100)===0n,'Independent layer failed');layerOpened=true;report.displayTouched=true;
  }
  if(wsi){
   await put(0x6000,Buffer.from('VK_KHR_surface\0'));await put(0x6040,Buffer.from('VK_NN_vi_surface\0'));
   const names=Buffer.alloc(16);names.writeBigUInt64LE(BigInt(p+0x6000));names.writeBigUInt64LE(BigInt(p+0x6040),8);await put(0x6080,names);
  }
  await put(0x100,struct(48,0,{44:(1<<22)|(4<<12)}));
  await put(0x200,struct(64,1,wsi?{48:2}:{}, {24:p+0x100,...wsi?{56:p+0x6080}:{}}));
  await ok('vkCreateInstance',p+0x200,0,p);instance=await pointer(0);
  await ok('vkEnumeratePhysicalDevices',instance,p+8,0);
  const count=(await rpc.read(p+8,4)).readUInt32LE();check(count===1,'Expected one physical device');
  await ok('vkEnumeratePhysicalDevices',instance,p+8,p+16);const physical=await pointer(16);
  if(present){
   const window=await call('_ZN2nn2vi5Layer15GetNativeWindowEv',layer);
   await put(0x6100,struct(32,1000062000,{}, {24:window}));await ok('vkCreateViSurfaceNN',instance,p+0x6100,0,p+104);surface=await pointer(104);
   await ok('vkGetPhysicalDeviceSurfaceCapabilitiesKHR',physical,surface,p+0x6200);
   const c=await rpc.read(p+0x6200,52),supportedCompositeAlpha=readSupportedCompositeAlpha(c);report.surface={width:c.readUInt32LE(8),height:c.readUInt32LE(12),minImages:c.readUInt32LE(),maxImages:c.readUInt32LE(4),transform:c.readUInt32LE(40),usage:c.readUInt32LE(48),supportedCompositeAlpha,composition:{requested:compositionRequest,supportedMask:supportedCompositeAlpha,selectedMode:null,selectedBit:null}};
   // Preserve the advertised mask/request even if explicit selection fails.
   await save();report.surface.composition=selectCompositeAlpha(supportedCompositeAlpha,compositionRequest);
   check(report.surface.width===1920&&report.surface.height===1080,'Independent surface is not 1080p');check((report.surface.usage&17)===17,'Surface cannot render and read back');await save();
   await ok('vkGetPhysicalDeviceSurfaceFormatsKHR',physical,surface,p+144,0);const n=(await rpc.read(p+144,4)).readUInt32LE();check(n>0&&n<=64,'Unexpected format count');
   await ok('vkGetPhysicalDeviceSurfaceFormatsKHR',physical,surface,p+144,p+0x6400);const formats=await rpc.read(p+0x6400,n*8);
   check(Array.from({length:n},(_,i)=>formats.readUInt32LE(i*8)===37&&formats.readUInt32LE(i*8+4)===0).some(Boolean),'RGBA8 surface unavailable');
  }
  await call('vkGetPhysicalDeviceQueueFamilyProperties',physical,p+24,0);
  const familyCount=(await rpc.read(p+24,4)).readUInt32LE();check(familyCount>0&&familyCount<=32,'Invalid family count');
  await call('vkGetPhysicalDeviceQueueFamilyProperties',physical,p+24,p+0x1000);
  const familyData=await rpc.read(p+0x1000,familyCount*24);
  let family=-1;for(let i=0;i<familyCount;i++)if(familyData.readUInt32LE(i*24)&(render?1:4)){family=i;break;}
  check(family>=0,'No transfer queue');report.queueFamily=family;
  await call('vkGetPhysicalDeviceMemoryProperties',physical,p+0x2000);
  const memoryProperties=await rpc.read(p+0x2000,1024),typeCount=memoryProperties.readUInt32LE();
  check(typeCount>0&&typeCount<=32,'Invalid memory type count');
  const priority=Buffer.alloc(4);priority.writeFloatLE(1);await put(0x300,priority);
  await put(0x400,struct(40,2,{20:family,24:1},{32:p+0x300}));
  // Enable the core 1.3 synchronization2 feature used by the host-read barrier.
  await put(0x500,struct(24,1000314007,{16:1},render?{8:p+0x540}:{}));
  if(render)await put(0x540,struct(24,1000044003,{16:1}));
  if(wsi){await put(0x6500,Buffer.from('VK_KHR_swapchain\0'));const b=Buffer.alloc(8);b.writeBigUInt64LE(BigInt(p+0x6500));await put(0x6540,b);}
  await put(0x600,struct(72,3,{20:1,...wsi?{48:1}:{}},{8:p+0x500,24:p+0x400,...wsi?{56:p+0x6540}:{}}));
  await ok('vkCreateDevice',physical,p+0x600,0,p+32);device=await pointer(32);report.deviceCreated=true;await save();
  if(mode==='inspect'){
   const dispatch=Number((await rpc.read(Number(device),8)).readBigUInt64LE());
   const target=Number((await rpc.read(dispatch+0x678,8)).readBigUInt64LE());
   check((await rpc.query(target)).permissions===5,'Teardown target is not executable');
   report.destroySwapchainImplementation=target;
   await fs.writeFile(path.join(directory,'vkDestroySwapchainKHR-implementation.bin'),await rpc.read(target,0x2000),{mode:0o600});
   console.log('Captured swapchain teardown implementation without creating a swapchain.');return;
  }
  await call('vkGetDeviceQueue',device,family,0,p+40);const queue=await pointer(40);
  if(present){
   await ok('vkGetPhysicalDeviceSurfaceSupportKHR',physical,family,surface,p+144);check((await rpc.read(p+144,4)).readUInt32LE()===1,'Queue cannot present');
   await put(0x6600,struct(104,1000001000,{32:report.surface.minImages,36:37,44:1920,48:1080,52:1,56:17,80:report.surface.transform,84:report.surface.composition.selectedBit,88:2,92:1},{24:surface}));
   await ok('vkCreateSwapchainKHR',device,p+0x6600,0,p+112);swapchain=await pointer(112);
   // Destruction crashed in the previous session. Keep this display's complete
   // object graph alive until the application exits; do not replay teardown.
   retainDisplayResources=true;
   report.displayResources={layer,display:display.toString(),instance:instance.toString(),device:device.toString(),surface:surface.toString(),swapchain:swapchain.toString()};await save();
   await put(0x6700,struct(24,9));await ok('vkCreateSemaphore',device,p+0x6700,0,p+120);acquireSemaphore=await pointer(120);
   await ok('vkCreateSemaphore',device,p+0x6700,0,p+128);presentSemaphore=await pointer(128);
   await put(0x6800,struct(56,1000060010,{48:1},{16:swapchain,24:1000000000n,32:acquireSemaphore}));
   await ok('vkAcquireNextImage2KHR',device,p+0x6800,p+136);imageIndex=(await rpc.read(p+136,4)).readUInt32LE();
   await ok('vkGetSwapchainImagesKHR',device,swapchain,p+144,0);const n=(await rpc.read(p+144,4)).readUInt32LE();check(n>=2&&n<=8&&imageIndex<n,'Invalid swapchain images');
   await ok('vkGetSwapchainImagesKHR',device,swapchain,p+144,p+0x6900);image=(await rpc.read(p+0x6900+imageIndex*8,8)).readBigUInt64LE();report.imageIndex=imageIndex;await save();
  }
  const mapped=[];
  for(let i=0;i<(render?1:2);i++){
   await put(0x700,struct(56,12,{32:render?2:i===0?1:2},{24:byteCount}));
   await ok('vkCreateBuffer',device,p+0x700,0,p+48);const buffer=await pointer(48);buffers.push(buffer);
   await call('vkGetBufferMemoryRequirements',device,buffer,p+0x800);
   const req=await rpc.read(p+0x800,24),size=req.readBigUInt64LE(),bits=req.readUInt32LE(16);
   check(size>=BigInt(byteCount)&&size<=33554432n,'Unexpected buffer allocation size');
   let type=-1;for(let j=0;j<typeCount;j++)if((bits&(1<<j))&&((memoryProperties.readUInt32LE(4+j*8)&6)===6)){type=j;break;}
   check(type>=0,'No host-visible coherent memory type');
   await put(0x900,struct(32,5,{24:type},{16:size}));
   await ok('vkAllocateMemory',device,p+0x900,0,p+56);const memory=await pointer(56);memories.push(memory);
   await ok('vkBindBufferMemory',device,buffer,memory,0);
   // Core Vulkan 1.4 MapMemory2 fits the bridge's four-argument ABI.
   await put(0xa00,struct(48,1000271000,{}, {24:memory,40:size}));
   await ok('vkMapMemory2',device,p+0xa00,p+64);const address=Number(await pointer(64));
   check(Number.isSafeInteger(address),'Invalid mapped address');mapped.push(address);
  }
  const source=render?null:crypto.randomBytes(byteCount);
  if(source)await rpc.write(mapped[0],source);
  await rpc.write(mapped[render?0:1],Buffer.alloc(byteCount,0x5a));
  if(render&&!present){
   await put(0x4000,struct(88,14,{20:1,24:37,28:1920,32:1080,36:1,40:1,44:1,48:1,56:0x11}));
   await ok('vkCreateImage',device,p+0x4000,0,p+88);image=await pointer(88);
   await call('vkGetImageMemoryRequirements',device,image,p+0x800);
   const req=await rpc.read(p+0x800,24),size=req.readBigUInt64LE(),bits=req.readUInt32LE(16);
   check(size>0n&&size<=67108864n,'Unexpected image allocation size');
   let type=-1;for(let j=0;j<typeCount;j++)if((bits&(1<<j))&&(memoryProperties.readUInt32LE(4+j*8)&1)){type=j;break;}
   check(type>=0,'No device-local image memory');
   await put(0x900,struct(32,5,{24:type},{16:size}));await ok('vkAllocateMemory',device,p+0x900,0,p+56);
   const memory=await pointer(56);memories.push(memory);await ok('vkBindImageMemory',device,image,memory,0);
  }
  if(render){
   await put(0x4100,struct(80,15,{32:1,36:37,56:1,64:1,72:1},{24:image}));
   await ok('vkCreateImageView',device,p+0x4100,0,p+96);imageView=await pointer(96);
  }
  await put(0xb00,struct(24,39,{20:family}));await ok('vkCreateCommandPool',device,p+0xb00,0,p+72);pool=await pointer(72);
  await put(0xc00,struct(32,40,{28:1},{16:pool}));await ok('vkAllocateCommandBuffers',device,p+0xc00,p+80);const command=await pointer(80);
  await put(0xd00,struct(32,42,{16:1}));await ok('vkBeginCommandBuffer',command,p+0xd00);
  if(render){
   async function transition(oldLayout,newLayout,srcStage,srcAccess,dstStage,dstAccess){
    await put(0x4200,struct(96,1000314002,{48:oldLayout,52:newLayout,56:0xffffffff,60:0xffffffff,72:1,80:1,88:1},
     {16:srcStage,24:srcAccess,32:dstStage,40:dstAccess,64:image}));
    await put(0x4300,struct(64,1000314003,{48:1},{56:p+0x4200}));
    await call('vkCmdPipelineBarrier2',command,p+0x4300);
   }
   await transition(0,2,0,0,0x400,0x100);
   // Visible startup stays black; offscreen render diagnostics retain the color pattern.
   const colors=present?Array(4).fill([0,0,0,1]):[[1,0,0,1],[0,1,0,1],[0,0,1,1],[1,1,1,1]];
   for(let i=0;i<4;i++){
    if(i)await transition(2,2,0x400,0x100,0x400,0x100);
    const attachment=struct(72,1000044001,{24:2,44:1},{16:imageView});
    colors[i].forEach((v,c)=>attachment.writeFloatLE(v,52+c*4));await put(0x4400,attachment);
    await put(0x4500,struct(72,1000044000,{20:(i%2)*960,24:Math.floor(i/2)*540,28:960,32:540,36:1,44:1},{48:p+0x4400}));
    await call('vkCmdBeginRendering',command,p+0x4500);await call('vkCmdEndRendering',command);
   }
   await transition(2,6,0x400,0x100,0x1000,0x800);
   await put(0x4600,struct(72,1000337009,{32:1,44:1,60:1920,64:1080,68:1}));
   await put(0x4700,struct(56,1000337003,{24:6,40:1},{16:image,32:buffers[0],48:p+0x4600}));
   await call('vkCmdCopyImageToBuffer2',command,p+0x4700);
   if(present)await transition(6,1000001002,0x1000,0x800,0,0);
  }else{
   await put(0xe00,struct(40,1000337006,{}, {32:byteCount}));
   await put(0xf00,struct(48,1000337000,{32:1},{16:buffers[0],24:buffers[1],40:p+0xe00}));
   await call('vkCmdCopyBuffer2',command,p+0xf00);
  }
  await put(0x3000,struct(48,1000314000,{}, {16:0x10000,24:0x1000,32:0x4000,40:0x2000}));
  await put(0x3100,struct(64,1000314003,{20:1},{24:p+0x3000}));
  await call('vkCmdPipelineBarrier2',command,p+0x3100);await ok('vkEndCommandBuffer',command);
  if(present){const mask=Buffer.alloc(4);mask.writeUInt32LE(0x400);await put(0x6a00,mask);}
  await put(0x3200,struct(72,4,{40:1,...present?{16:1,56:1}:{}},{48:p+80,...present?{24:p+120,32:p+0x6a00,64:p+128}:{}}));
  submitted=true;await ok('vkQueueSubmit',queue,1,p+0x3200,0);await ok('vkQueueWaitIdle',queue);finished=true;
  const result=await rpc.read(mapped[render?0:1],byteCount);
  if(render){
   const colors=present?Array(4).fill([0,0,0,255]):[[255,0,0,255],[0,255,0,255],[0,0,255,255],[255,255,255,255]];let mismatches=0;
   for(let y=0;y<1080;y++)for(let x=0;x<1920;x++){
    const color=colors[(y>=540?2:0)+(x>=960?1:0)],at=(y*1920+x)*4;
    if(color.some((v,c)=>result[at+c]!==v))mismatches++;
   }
   report.width=1920;report.height=1080;report.mismatchedPixels=mismatches;report.verified=mismatches===0;
   await fs.writeFile(path.join(directory,'vulkan-1920x1080.rgba'),result,{mode:0o600});
  }else{report.verified=result.equals(source);report.sourceSha256=crypto.createHash('sha256').update(source).digest('hex');}
  report.destinationSha256=crypto.createHash('sha256').update(result).digest('hex');
  await save();check(report.verified,'GPU result mismatch');
  console.log(render?'Verified direct Vulkan 1920x1080 render: all pixels match.':'Verified direct Vulkan GPU transfer: 65,536 bytes match.');
  if(present){
   await put(0x6b00,struct(64,1000001001,{16:1,32:1},{24:p+128,40:p+112,48:p+136}));
   await ok('vkQueuePresentKHR',queue,p+0x6b00);report.presentationAccepted=true;report.presentationAcceptedAt=new Date().toISOString();await save();
   report.displayResources.acquiredSemaphore=acquireSemaphore.toString();report.displayResources.presentSemaphore=presentSemaphore.toString();
   report.displayResources.queue=queue.toString();report.phase='display-held';await save();
   console.log('Default-tag 1080p surface submitted. Resources remain alive until SmileBASIC exits.');
  }
 }catch(e){report.error=e.stack;process.exitCode=1;}
 finally{
  report.cleanupErrors=[];
  if(!retainDisplayResources&&(!submitted||finished)){
   const actions=[...device?[['commandPool',async()=>{if(pool)await call('vkDestroyCommandPool',device,pool,0);}]]:[],
    ['imageView',async()=>{if(imageView)await call('vkDestroyImageView',device,imageView,0);}],
    ['image',async()=>{if(image&&!swapchain)await call('vkDestroyImage',device,image,0);}],
    ...buffers.map(b=>['buffer',()=>call('vkDestroyBuffer',device,b,0)]),
    ...memories.map(m=>['memory',()=>call('vkFreeMemory',device,m,0)]),
    ['swapchain',async()=>{if(swapchain)await call('vkDestroySwapchainKHR',device,swapchain,0);}],
    ['acquireSemaphore',async()=>{if(acquireSemaphore)await call('vkDestroySemaphore',device,acquireSemaphore,0);}],
    ['presentSemaphore',async()=>{if(presentSemaphore)await call('vkDestroySemaphore',device,presentSemaphore,0);}],
    ['device',async()=>{if(device)await call('vkDestroyDevice',device,0);}],
    ['surface',async()=>{if(surface)await call('vkDestroySurfaceKHR',instance,surface,0);}],
    ['layer',async()=>{if(layerOpened){await call('_ZN2nn2vi5Layer5CloseEb',layer,0);check((await rpc.read(layer+0x500,2)).equals(Buffer.alloc(2)),'Layer retains resources');}}],
    ['display',async()=>{if(display)await call('_ZN2nn2vi12CloseDisplayEPNS0_7DisplayE',display);}],
    ['layerAllocation',async()=>{if(layer)await sdkCall('free',layer);}],
    ['instance',async()=>{if(instance)await call('vkDestroyInstance',instance,0);}]];
   for(const [name,action]of actions)try{await action();}catch(e){report.cleanupErrors.push({name,error:e.message});break;}
   report.resourcesReleased=!report.cleanupErrors.length;
  }else report.resourcesRetained=true;
  try{await put(0,saved);report.scratchRestored=(await rpc.read(p,saved.length)).equals(saved);}catch(e){report.cleanupErrors.push({name:'scratch',error:e.message});}
  report.completed=new Date().toISOString();await save();console.log(file);if(report.error)console.error(report.error);
 }
}
if(require.main===module)main(process.argv[2],process.argv[3],process.argv[4]).catch(e=>{console.error(e.stack);process.exitCode=1;});
