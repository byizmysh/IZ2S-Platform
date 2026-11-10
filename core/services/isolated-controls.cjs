'use strict';
// Called inside the GPU preparer's existing owned scratch/worker boundary.
// The parent descriptor layout is borrowed; every buffer/memory/set is new.
const assert=require('node:assert/strict'),BYTES=8388608,LIMIT=2**39;
const struct=(n,type,words={},pointers={})=>{const b=Buffer.alloc(n);b.writeUInt32LE(type);for(const [at,v]of Object.entries(words))b.writeUInt32LE(v,+at);for(const [at,v]of Object.entries(pointers))b.writeBigUInt64LE(BigInt(v),+at);return b;};
function retained(message){return Object.assign(Error(message),{resourcesRetained:true});}
async function allocate({rpc,menu,p,put,ptr,call,ok,properties,record,protectedRanges=[]}){
 assert(menu.controls?.bytes===BYTES&&BigInt(menu.controls.setLayout||0)>0n,'Isolated controls require the original one-binding descriptor layout');
 assert(Number.isSafeInteger(menu.controls.mapped)&&menu.controls.mapped>=4096,'Invalid parent control mapping');
 const device=BigInt(menu.resources.device),flags=properties,types=flags.readUInt32LE();assert(types>=1&&types<=32,'Invalid coherent memory property count');
 const out=record.controlStorage={format:'iz2s.isolated-controls/1',bytes:BYTES,parentMapped:menu.controls.mapped,setLayout:String(menu.controls.setLayout),borrowedSetLayout:true,resourcesRetained:true,ready:false};
 async function handle(offset,label){const v=await ptr(offset);if(v<=0n||v>0xffffffffffffffffn)throw retained('Invalid isolated control '+label+' handle');return v;}
 await put(0x100,struct(56,12,{32:0x20},{24:BYTES}));await put(0,Buffer.alloc(40));await ok('vkCreateBuffer',device,p+0x100,0,p);out.buffer=String(await handle(0,'buffer'));
 await call('vkGetBufferMemoryRequirements',device,BigInt(out.buffer),p+0x200);const req=await rpc.read(p+0x200,24),size=req.readBigUInt64LE(),bits=req.readUInt32LE(16);let type=-1;
 for(const required of [7,6]){for(let i=0;i<types;i++)if((bits&(1<<i))&&((flags.readUInt32LE(4+i*8)&required)===required)){type=i;break;}if(type>=0)break;}
 if(type<0||size<BigInt(BYTES)||size>BigInt(BYTES+65536))throw retained('Invalid isolated control storage requirements');out.allocationBytes=Number(size);out.memoryType=type;out.memoryFlags=flags.readUInt32LE(4+type*8);
 await put(0x100,struct(32,5,{24:type},{16:size}));await ok('vkAllocateMemory',device,p+0x100,0,p+8);out.memory=String(await handle(8,'memory'));
 await ok('vkBindBufferMemory',device,BigInt(out.buffer),BigInt(out.memory),0);
 await put(0x100,struct(48,1000271000,{}, {24:BigInt(out.memory),40:size}));await ok('vkMapMemory2',device,p+0x100,p+16);out.mapped=Number(await ptr(16));
 if(!Number.isSafeInteger(out.mapped)||out.mapped<4096||out.mapped%4096||out.mapped>LIMIT-BYTES)throw retained('Invalid isolated control mapping');
 for(const range of [{base:menu.controls.mapped,bytes:menu.controls.bytes},{base:p,bytes:65536},...protectedRanges]){
  assert(Number.isSafeInteger(range.base)&&Number.isSafeInteger(range.bytes)&&range.base>=4096&&range.bytes>0&&Number.isSafeInteger(range.base+range.bytes),'Invalid protected control range');
  if(out.mapped<range.base+range.bytes&&range.base<out.mapped+BYTES)throw retained('Isolated control mapping overlaps another owner');
 }
 try{await require('../console/sd-launcher/host-frames.cjs').verifyControls(rpc,out);}catch(error){error.resourcesRetained=true;throw error;}
 const pool=Buffer.alloc(8);pool.writeUInt32LE(7);pool.writeUInt32LE(1,4);await put(0x200,pool);await put(0x100,struct(40,33,{20:1,24:1},{32:p+0x200}));await ok('vkCreateDescriptorPool',device,p+0x100,0,p+24);out.descriptorPool=String(await handle(24,'descriptor pool'));
 const layout=Buffer.alloc(8);layout.writeBigUInt64LE(BigInt(menu.controls.setLayout));await put(0x200,layout);await put(0x100,struct(40,34,{24:1},{16:BigInt(out.descriptorPool),32:p+0x200}));await ok('vkAllocateDescriptorSets',device,p+0x100,p+32);out.descriptor=String(await handle(32,'descriptor'));
 const info=Buffer.alloc(24);info.writeBigUInt64LE(BigInt(out.buffer));info.writeBigUInt64LE(BigInt(BYTES),16);await put(0x200,info);await put(0x100,struct(64,35,{24:0,32:1,36:7},{16:BigInt(out.descriptor),48:p+0x200}));await call('vkUpdateDescriptorSets',device,1,p+0x100,0,0);
 out.ready=true;return out;
}
module.exports={allocate,BYTES};
