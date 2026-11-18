const D=require('./block-delta.cjs'),S=require('./pc-screen.cjs'),{build}=require('./platform.cjs').require('core/console/abi/frames.cjs'),code=require('./platform.cjs').require('core/console/abi/thread-code.json').functions;
function programs({base,sdk,mapped,video={base:mapped+S.SLOT,stride:S.STRIDE,capacity:S.STRIDE},width,height,format=1,functions,strm,directInflate=require('./inflate-policy.cjs').directInflateFor(width,height),profileDecode=process.env.IZ12_STREAM_PROFILE==='1',heapDictionary=process.env.IZ2S_HEAP_DICTIONARY==='1',allowAllBc1HeapDictionary=false}){
 const bytes=S.frameBytes(width,height,format);if(bytes>video.capacity)throw Error('Frame exceeds video storage');const layout=D.validateBase(base,bytes),selector=mapped+S.HEADER+32,fd=base+0x2300,staging=base+D.STAGING,temp=base+0x500000;
 heapDictionary=!!(heapDictionary&&!directInflate&&(allowAllBc1HeapDictionary||width===1920&&height===1080)&&format===1);
 if(!Number.isSafeInteger(strm)||strm<4096||strm%8)throw Error('Invalid owned z_stream');
 const addresses={boot:base+0x10000,zero:base+0x90000,one:base+0x110000,wait:base+0x190000,exit:base+0x210000};
 const constants=Buffer.alloc(40);constants.writeBigUInt64LE(2n);constants.writeBigUInt64LE(3n,8);constants.writeBigUInt64LE(BigInt(addresses.wait),16);constants.writeBigUInt64LE(2n,24);constants.writeBigUInt64LE(3n,32);
 const output=Buffer.alloc(16);output.writeBigUInt64LE(BigInt(temp));output.writeUInt32LE(D.BLOCK,8);
 const telemetry=base+0x2a00,stages=Buffer.alloc(48);for(let i=0;i<6;i++)stages.writeBigUInt64LE(BigInt(i+1),i*8);
 const acknowledgementBytes=profileDecode?436:396,timingAddress=base+0x2b00,tick=sdk+require('./platform.cjs').require('core/console/abi/timing-code.json').functions.tick.offset;
 const copy=sdk+0x586fb0,jump=sdk+code.longjmp.offset,verified=new Set([...Object.values(functions),...(profileDecode?[tick]:[])]);
 function make(name,slot,ack){
  const at=addresses[name],snapshot=at+0x300000,calls=[],results=[],outputFixups=[];
  const add=(fn,args=[])=>{calls.push({function:fn,args:[...args,...Array(4-args.length).fill(0)]});return calls.length-1;};
  const mark=n=>add(copy,[telemetry,base+0x2800+(n-1)*8,8]);
  const stamp=(offset,ownSlot=slot)=>{if(profileDecode)results.push([add(tick,[]),timingAddress+ownSlot*64+offset]);};
  if(ack!==null){stamp(32,ack&1);mark(6);add(copy,[base+0x2100,base+0x2000+(ack===2?0:8),8]);add(copy,[base+0x2108,mapped+0x7f0280,320]);add(copy,[base+0x2248,mapped+0x700004,4]);add(copy,[base+0x224c,mapped+S.HEADER+96,8]);add(copy,[base+0x2254,mapped+128,56]);if(profileDecode)add(copy,[base+0x228c,timingAddress+(ack&1)*64,40]);const sent=add(functions.send,[{u32:fd},base+0x2100,acknowledgementBytes,0]);results.push([sent,telemetry+40]);}
  add(copy,[selector,base+0x2010,8]);
  if(ack===null){const socket=add(functions.socket,[2,1,6]);results.push([socket,fd]);const connect=add(functions.connect,[{u32:fd},base+0x2400,16]);results.push([connect,telemetry+32]);}
  stamp(0);mark(1);const receive=add(functions.recv,[{u32:fd},staging,layout.header,0x40]);results.push([receive,telemetry+8]);
  // Explicit length, persistent TCP: no per-frame connect/FIN/EOF handshake.
  // Header length is fixed. Body length comes only from the trusted PC encoder.
  // Every possible partial old/new length stays <0x100000 in owned staging.
  mark(2);const bodyReceive=add(functions.recv,[{u32:fd},staging+layout.header,{u32:staging+20},0x40]);results.push([bodyReceive,telemetry+16]);
  stamp(8);mark(3);
  for(let i=0;i<layout.count;i++){
   const b=layout.blocks[i],dest=video.base+slot*video.stride+b.offset,dictionary=heapDictionary?base+D.HEAP_DICTIONARY+slot*D.HEAP_STRIDE+b.offset:dest;
   const reset=add(functions.reset,[strm]);results.push([reset,telemetry+48]);const dict=add(functions.dictionary,[strm,dictionary,b.length]);results.push([dict,telemetry+56]);
   add(copy,[strm,staging+64+i*16,16]);
   if(directInflate)outputFixups.push({call:add(copy,[strm+24,0,16]),dest,bytes:b.length});
   else add(copy,[strm+24,base+0x2600+(b.length===D.BLOCK?0:16),16]);
   const inflated=add(functions.inflate,[strm,4]);results.push([inflated,telemetry+24]);
   if(!directInflate){if(heapDictionary)add(copy,[dictionary,temp,{u32:staging+76+i*16}]);add(copy,[dest,temp,{u32:staging+76+i*16}]);}
  }
  stamp(16);mark(4);const crc=add(functions.crc32,[0,video.base+slot*video.stride,bytes]);results.push([crc,mapped+S.HEADER+64]);
  add(copy,[mapped+S.HEADER+72,staging+8,4]);add(copy,[mapped+S.HEADER+104,staging+32,32]);
  stamp(24);mark(5);add(copy,[mapped+S.HEADER,base+0x2018+slot*8,4]);
  const restore={function:copy,args:[at,snapshot,0,0]};calls.push(restore,{function:jump,args:[{u64:selector},1,0,0]});
  let p=build(at,sdk,calls,code,false,verified);
  // The frame builder reserves a 64KiB gap before its stack. Immutable IO
  // templates fit there and are restored with the frame; no per-frame RPC.
  const outputOffset=p.proof.stackOffset-0x10000;
  if(outputFixups.length*16>0x10000||outputOffset<p.proof.tablesOffset+p.proof.nativeCalls*0x100)throw Error('Direct inflate templates overlap native frame');
  for(let i=0;i<outputFixups.length;i++)calls[outputFixups[i].call].args[1]=at+outputOffset+i*16;
  restore.args[2]=p.frame.length;p=build(at,sdk,calls,code,false,verified);
  for(let i=0;i<outputFixups.length;i++){const item=outputFixups[i],offset=outputOffset+i*16;p.frame.writeBigUInt64LE(BigInt(item.dest),offset);p.frame.writeUInt32LE(item.bytes,offset+8);}
  for(const [i,dest] of results){const native=p.proof.logicalResultIndices[i];p.frame.writeBigUInt64LE(BigInt(dest),p.proof.stackOffset+native*0x140+0x118);}
  if(p.frame.length>0x80000)throw Error('Delta receiver graph exceeds owned slot');
  return {...p,at,snapshot,slot,ack,calls,receive,bodyReceive,directInflate,heapDictionary,outputOffset:directInflate?outputOffset:null};
 }
 const waitCalls=[{function:sdk+code.sleep.offset,args:[500000,0,0,0]},{function:copy,args:[addresses.wait,addresses.wait+0x300000,0,0]},{function:jump,args:[{u64:selector},1,0,0]}];
 let wait=build(addresses.wait,sdk,waitCalls,code,false,verified);waitCalls[1].args[2]=wait.frame.length;wait=build(addresses.wait,sdk,waitCalls,code,false,verified);
 const exit=Buffer.alloc(0xb0);exit.writeBigUInt64LE(BigInt(sdk+code.exit.offset),0x58);exit.writeBigUInt64LE(BigInt(base+0x280000),0x68);
 const header=Buffer.alloc(96);header.writeUInt32LE(width,4);header.writeUInt32LE(height,8);header.writeUInt32LE(format,24);header.writeBigUInt64LE(BigInt(addresses.wait),32);header.writeBigUInt64LE(BigInt(addresses.zero),40);header.writeBigUInt64LE(BigInt(addresses.one),48);header.writeBigUInt64LE(BigInt(addresses.exit),56);header.writeUInt32LE(1,76);
 const tail=Buffer.from(output);tail.writeUInt32LE(layout.blocks.at(-1).length,8);
 return {addresses,constants,stages,telemetry,acknowledgementBytes,timingAddress:profileDecode?timingAddress:null,directInflate,heapDictionary,output:Buffer.concat([output,tail]),header,exit,bytes,layout,fd,frames:[make('boot',0,null),make('zero',0,3),make('one',1,2),{...wait,at:addresses.wait,snapshot:addresses.wait+0x300000,calls:waitCalls}]};
}
module.exports={programs};
