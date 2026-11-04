'use strict';
// Resident control ABI 2. All offsets below are words, never native pointers.
// ABI 1 bundles must be regenerated: reset extents are explicit in ABI 2.
const MAGIC=0x49525231,VERSION=2,MAX_SLOTS=16,HEADER_WORDS=64,TABLE=512,STRIDE=64;
const STATE_BYTES=8*1024*1024,INPUT_BYTES=STATE_BYTES,CLOCK=0x700c80+240,HZ=31250000;
const H={magic:0,version:1,count:2,active:3,previous:4,request:5,generation:6,faults:7,
 frames:8,inputs:9,buttons:10,release:11,minusArmed:12,minusLow:13,minusHigh:14,
 tickLow:16,tickHigh:17,frequency:18,deltaLow:19,lastLow:20,lastHigh:21,clockValid:22,
 transitions:23,changed:24,requests:25,invalidRequests:26};
const S={enabled:0,stateWords:1,privateWords:2,x:3,y:4,z:5,sequence:6,reset:7,
 resetDispatch:8,inputDispatch:12,beginDispatch:16,mainDispatch:20,afterDispatch:24,
 postDispatch:28,draw:32,initialStateWords:36,initialPrivateWords:37,tickLow:38,tickHigh:39,
 launches:40,frames:41,privateResetWords:42,keepFirst:43,keepEnd:44,wallClock:45,service:46,privateKeepFirst:47,privateKeepEnd:48};
const FAULT={badActive:1,badRequest:2,badClock:4};
const word=(slot,field)=>TABLE+slot*STRIDE+field;
function make(slots){
 if(!Array.isArray(slots)||slots.length<2||slots.length>MAX_SLOTS)throw Error('Resident session requires menu and 1..15 apps');
 const b=Buffer.alloc((TABLE+MAX_SLOTS*STRIDE)*4);
 b.writeUInt32LE(MAGIC);b.writeUInt32LE(VERSION,4);b.writeUInt32LE(slots.length,8);b.writeUInt32LE(1,H.release*4);
 slots.forEach((slot,i)=>{
  const grid=slot.grid||[1,1,1],privateBytes=slot.privateBytes||4;
  if(!Array.isArray(grid)||grid.length!==3||!grid.every(n=>Number.isInteger(n)&&n>0&&n<=65535)||grid.reduce((a,b)=>a*b,1)>4096)throw Error('Invalid resident dispatch grid');
  if(!Number.isInteger(privateBytes)||privateBytes<4||privateBytes>128*1024*1024||privateBytes%4)throw Error('Invalid resident private storage');
  const values={[S.enabled]:slot.enabled===false?0:1,[S.stateWords]:STATE_BYTES/4,[S.privateWords]:privateBytes/4,
   [S.x]:grid[0],[S.y]:grid[1],[S.z]:grid[2],[S.sequence]:slot.sequence?1:0,[S.reset]:i===0?0:1,
   [S.initialStateWords]:(slot.initialStateBytes??STATE_BYTES)/4,[S.initialPrivateWords]:(slot.initialPrivateBytes??privateBytes)/4,
   [S.privateResetWords]:(slot.privateResetBytes??privateBytes)/4,[S.keepFirst]:(slot.keepControlFirst??0)/4,[S.keepEnd]:(slot.keepControlEnd??0)/4,
   [S.wallClock]:slot.wallClock?1:0,[S.service]:slot.service||0,
   [S.privateKeepFirst]:(slot.privateKeepFirst??0)/4,[S.privateKeepEnd]:(slot.privateKeepEnd??0)/4};
  for(const [field,value] of Object.entries(values)){
   if(!Number.isInteger(value)||value<0||value>0xffffffff)throw Error('Invalid resident slot metadata');
   b.writeUInt32LE(value,word(i,Number(field))*4);
  }
  if(values[S.initialStateWords]>STATE_BYTES/4||values[S.initialPrivateWords]>privateBytes/4)throw Error('Resident initial image exceeds slot allocation');
  if(values[S.privateResetWords]>privateBytes/4||values[S.keepFirst]>values[S.keepEnd]||values[S.keepEnd]>STATE_BYTES/4||values[S.service]>1)throw Error('Invalid resident persistent memory policy');
  if(values[S.privateKeepFirst]>values[S.privateKeepEnd]||values[S.privateKeepEnd]>values[S.privateResetWords])throw Error('Invalid resident persistent cache policy');
 });
 return b;
}
module.exports={MAGIC,VERSION,MAX_SLOTS,HEADER_WORDS,TABLE,STRIDE,STATE_BYTES,INPUT_BYTES,CLOCK,HZ,H,S,FAULT,word,make};
