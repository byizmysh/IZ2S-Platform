'use strict';
// Standard Vulkan recording only. App choice is an indirect-buffer index on GPU.
const L=require('./layout.cjs');
const {recordPresentation}=require('../../../core/services/gpu-presentation.cjs');
const struct=(size,type,words={},pointers={})=>{
 const b=Buffer.alloc(size);b.writeUInt32LE(type);for(const [at,value] of Object.entries(words))b.writeUInt32LE(value,Number(at));
 for(const [at,value] of Object.entries(pointers))b.writeBigUInt64LE(BigInt(value),Number(at));return b;
};
async function record({call,ok,put,ptr,p,device,pool,images,views,previewBuffer,capture=false,resident,slots}){
 if(slots.length<2||slots.length>L.MAX_SLOTS||images.length!==views.length||!images.length)throw Error('Invalid resident frame resources');
 const offset=(slot,field)=>L.word(slot,field)*4;
 async function barrier(command,srcStage,srcAccess,dstStage,dstAccess){
  await put(0x1200,struct(48,1000314000,{}, {16:srcStage,24:srcAccess,32:dstStage,40:dstAccess}));
  await put(0x1300,struct(64,1000314003,{20:1},{24:p+0x1200}));await call('vkCmdPipelineBarrier2',command,p+0x1300);
 }
 async function bind(command,layout,descriptor){
  const value=Buffer.alloc(8);value.writeBigUInt64LE(BigInt(descriptor));await put(0x1180,value);
  await put(0x1100,struct(64,1000545003,{16:49,36:1},{24:layout,40:p+0x1180}));await call('vkCmdBindDescriptorSets2',command,p+0x1100);
 }
 async function select(command,id,name){
  await bind(command,resident.layout,slots[id].residentDescriptor);
  const value=Buffer.alloc(4);value.writeUInt32LE(id);await put(0x1190,value);
  await call('vkCmdPushConstants',command,BigInt(resident.layout),32,0,4,p+0x1190);
  await call('vkCmdBindPipeline',command,1,BigInt(resident.pipelines[name]));
 }
 const dispatch=(command,id,field)=>call('vkCmdDispatchIndirect',command,BigInt(resident.buffer),offset(id,field));
 const commands=[];
 for(let image=0;image<images.length;image++){
  await put(0x100,struct(32,40,{28:1},{16:pool}));await ok('vkAllocateCommandBuffers',BigInt(device),p+0x100,p);const command=await ptr(0);
  await put(0x200,struct(32,42));await ok('vkBeginCommandBuffer',command,p+0x200);
  await barrier(command,0x4800,0x4040,0x800,0x60);
  await select(command,0,'router');await call('vkCmdDispatch',command,1,1,1);
  await barrier(command,0x800,0x40,0x802,0x61);
  for(const [id,slot] of slots.entries()){
   await select(command,id,'slot-reset');await dispatch(command,id,L.S.resetDispatch);await barrier(command,0x800,0x40,0x800,0x60);
   await select(command,id,'slot-input');await dispatch(command,id,L.S.inputDispatch);await barrier(command,0x800,0x40,0x800,0x60);
   if(slot.service){await select(command,id,'service-control');await call('vkCmdDispatch',command,1,1,1);await barrier(command,0x800,0x40,0x800,0x60);}
   await bind(command,slot.layout,slot.descriptor);
   if(slot.sequence){await call('vkCmdBindPipeline',command,1,BigInt(slot.compute.begin));await dispatch(command,id,L.S.beginDispatch);await barrier(command,0x800,0x40,0x800,0x60);}
   for(let repeat=0;repeat<(slot.sequence?.repeat||1);repeat++){
    await call('vkCmdBindPipeline',command,1,BigInt(slot.compute.main));await dispatch(command,id,L.S.mainDispatch);await barrier(command,0x800,0x40,0x800,0x60);
    if(slot.sequence){await call('vkCmdBindPipeline',command,1,BigInt(slot.compute.after));await dispatch(command,id,L.S.afterDispatch);await barrier(command,0x800,0x40,0x800,0x60);}
   }
   await select(command,id,'slot-post');await dispatch(command,id,L.S.postDispatch);await barrier(command,0x800,0x40,0x888,0x60);
  }
  async function imageBarrier(oldLayout,newLayout,srcStage,srcAccess,dstStage,dstAccess){
   await put(0x300,struct(96,1000314002,{48:oldLayout,52:newLayout,56:0xffffffff,60:0xffffffff,72:1,80:1,88:1},
    {16:srcStage,24:srcAccess,32:dstStage,40:dstAccess,64:images[image]}));
   await put(0x400,struct(64,1000314003,{48:1},{56:p+0x300}));await call('vkCmdPipelineBarrier2',command,p+0x400);
  }
  await imageBarrier(0,2,0,0,0x400,0x100);
  const attachment=struct(72,1000044001,{24:2,44:1},{16:views[image]});attachment.writeFloatLE(1,64);await put(0x500,attachment);
  await put(0x600,struct(72,1000044000,{28:1920,32:1080,36:1,44:1},{48:p+0x500}));await call('vkCmdBeginRendering',command,p+0x600);
  for(const [id,slot] of slots.entries()){
   await bind(command,slot.layout,slot.descriptor);await call('vkCmdBindPipeline',command,0,BigInt(slot.graphics));
   await call('vkCmdDrawIndirect',command,BigInt(resident.buffer),offset(id,L.S.draw),1,16);
  }
  await call('vkCmdEndRendering',command);
  await recordPresentation({capture,command,image:images[image],previewBuffer,p,put,call,
   memoryBarrier:(...args)=>barrier(command,...args),barrier:imageBarrier});
  await barrier(command,0x800,0x40,0x4000,0x2000);await ok('vkEndCommandBuffer',command);commands.push(String(command));
 }
 return [commands];
}
module.exports={record,struct};
