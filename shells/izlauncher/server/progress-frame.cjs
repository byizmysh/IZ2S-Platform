'use strict';
// One ordinary Vulkan menu presentation at an explicitly stopped render boundary.
// Uses the existing prepared Vulkan functions and command buffers, without a worker or loader change.
const struct=(n,type,words={},ptrs={})=>{const b=Buffer.alloc(n);b.writeUInt32LE(type);for(const[k,v]of Object.entries(words))b.writeUInt32LE(v,+k);for(const[k,v]of Object.entries(ptrs))b.writeBigUInt64LE(BigInt(v),+k);return b;};
async function present({rpc,menu,idle}){
 if(!idle())throw Error('Progress frame requires a stopped renderer');
 const names=['vkAcquireNextImage2KHR','vkQueueSubmit','vkQueuePresentKHR','vkQueueWaitIdle'];
 if(names.some(n=>!menu.functions?.[n]))throw Error('Prepared Vulkan presentation unavailable');
 const p=rpc.a.scratch+0x10000,saved=await rpc.read(p,4096),r=menu.resources;
 const put=(offset,b)=>rpc.write(p+offset,b),call=async(name,...args)=>BigInt.asIntN(32,(await rpc.callAddress(menu.functions[name],args)).result);
 let acquired=false,finished=false;
 const ok=async(name,...args)=>{if(await call(name,...args)!==0n)throw Error(name+' progress presentation failed');};
 try{
  await put(0x100,struct(56,1000060010,{48:1},{16:r.swapchain,24:1000000000n,32:r.acquired}));
  const result=await call('vkAcquireNextImage2KHR',BigInt(r.device),p+0x100,p);
  if(result===2n)return false;
  if(result!==0n&&result!==1000001003n)throw Error('Progress image acquisition failed');acquired=true;
  const index=(await rpc.read(p,4)).readUInt32LE(),command=menu.commands[0]?.[index],semaphore=r.semaphores[index];
  if(command===undefined||semaphore===undefined)throw Error('Invalid progress image index');
  const handles=Buffer.alloc(40);handles.writeBigUInt64LE(BigInt(r.acquired));handles.writeBigUInt64LE(BigInt(command),8);handles.writeBigUInt64LE(BigInt(semaphore),16);handles.writeBigUInt64LE(BigInt(r.swapchain),24);handles.writeUInt32LE(0x400,32);await put(0x200,handles);
  await put(0x300,struct(72,4,{16:1,40:1,56:1},{24:p+0x200,32:p+0x220,48:p+0x208,64:p+0x210}));
  await ok('vkQueueSubmit',BigInt(r.queue),1,p+0x300,0);
  await put(0x400,struct(64,1000001001,{16:1,32:1},{24:p+0x210,40:p+0x218,48:p}));
  const shown=await call('vkQueuePresentKHR',BigInt(r.queue),p+0x400);if(shown!==0n&&shown!==1000001003n)throw Error('Progress presentation failed');
  await ok('vkQueueWaitIdle',BigInt(r.queue));finished=true;return true;
 }catch(error){if(acquired&&!finished)error.restartRequired=true;throw error;}
 finally{await put(0,saved);if(!(await rpc.read(p,saved.length)).equals(saved)){const error=Error('Progress frame scratch restoration failed');error.restartRequired=true;throw error;}}
}
module.exports={present};
