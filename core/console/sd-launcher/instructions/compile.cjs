'use strict';
// Bounded local controller instructions. No source evaluation, raw addresses,
// arbitrary SDK calls or branches; every memory operand belongs to this app.
const assert=require('node:assert/strict');
const SPACE={controls:0,private:1},COMPARE={eq:0,ne:1,lte:2,gte:3},OP={set:1,copy:2,read:3,write:4,exit:5,'audio-queue':6,'audio-start':7,reconnect:8};
const uint=(n,label)=>{assert(Number.isInteger(n)&&n>=0&&n<=0xffffffff,'Invalid '+label);return n;};
function keys(o,allowed){assert(o&&typeof o==='object'&&!Array.isArray(o),'Invalid instruction object');for(const k of Object.keys(o))assert(allowed.includes(k),'Unknown instruction field: '+k);}
function region(r,bytes,{write=false}={}){
 keys(r,['space','offset']);assert(Object.hasOwn(SPACE,r.space),'Invalid instruction memory space');uint(r.offset,'memory offset');uint(bytes,'memory size');assert(bytes>0);
 const limit=r.space==='private'?128*1024*1024:0x700000;assert(r.offset<=limit&&bytes<=limit-r.offset,'Instruction exceeds application memory');
 if(write&&r.space==='controls')assert((r.offset>=784&&r.offset+bytes<=960)||(r.offset>=1024&&r.offset+bytes<=0x700000),'Instructions may write only app state or app data');
 return [SPACE[r.space],r.offset];
}
function filename(name){assert(typeof name==='string'&&Buffer.byteLength(name)<128&&!/[\x00-\x1f\x7f\\:]/.test(name)&&name.split('/').every(p=>p&&p!=='.'&&p!=='..'),'Invalid local instruction file');assert(!/^\.?launcher\.izapp(?:\.|$)/.test(name)&&!/^\.install/.test(name),'Instructions cannot replace their own executable');return name;}
function compile(plan,{privateBytes=128*1024*1024}={}){
 keys(plan,['format','version','guards','initialize','rules','stop','assets']);assert.equal(plan.format,'iz2s.local-instructions');assert.equal(plan.version,1);
 if(plan.assets!==undefined)require('../assets.cjs').declarations({native:{assets:plan.assets}});
 const conditions=[],operations=[],rules=[],strings=[];let stringBytes=0;
 function condition(c){keys(c,['space','offset','compare','value','mask']);const [space,offset]=region({space:c.space,offset:c.offset},4);assert(offset%4===0);assert(Object.hasOwn(COMPARE,c.compare||'eq'));const value=uint(c.value,'condition value'),mask=c.mask===undefined?0xffffffff:uint(c.mask,'condition mask');assert(((value&mask)>>>0)===value,'Condition value exceeds mask');conditions.push([space,offset,COMPARE[c.compare||'eq'],value,mask,0]);}
 function action(a){
  assert(Object.hasOwn(OP,a?.op),'Unsupported local instruction');let out=[OP[a.op],0,0,0,0,0,0,0,0,0];
  if(a.op==='set'){keys(a,['op','to','value']);const [s,o]=region(a.to,4,{write:true});assert(o%4===0);out[1]=s;out[2]=o;out[6]=uint(a.value,'set value');}
  else if(a.op==='copy'){keys(a,['op','to','from','bytes']);uint(a.bytes,'copy size');assert(a.bytes<=65536);const [ds,d]=region(a.to,a.bytes,{write:true}),[ss,s]=region(a.from,a.bytes);assert((ds!==ss)||(d+a.bytes<=s||s+a.bytes<=d),'Instruction copy regions overlap');out.splice(1,5,ds,d,ss,s,a.bytes);}
  else if(a.op==='read'||a.op==='write'){
   keys(a,['op','path','offset','bytes',a.op==='read'?'to':'from']);uint(a.bytes,'file size');assert(a.bytes>0&&a.bytes<=16*1024*1024);if(a.op==='write')assert(a.bytes<=65536,'A local save must fit one bounded SD transaction');const [s,o]=region(a.op==='read'?a.to:a.from,a.bytes,{write:a.op==='read'});out[1]=s;out[2]=o;out[4]=a.offset===undefined?0:uint(a.offset,'file offset');if(a.op==='write')assert(out[4]===0,'Local saves replace one complete file');out[5]=a.bytes;
   const name=filename(a.path);if(a.op==='write')assert(!name.startsWith('asset-'),'Instructions cannot overwrite immutable companion assets');out[7]=stringBytes;const b=Buffer.from(name+'\0');strings.push(b);stringBytes+=b.length;
  }else if(a.op==='audio-queue'){
   keys(a,['op','offset','capacity','bytes','descriptors']);uint(a.offset,'PCM offset');uint(a.capacity,'PCM capacity');uint(a.bytes,'PCM bytes');assert(a.offset%4096===0&&a.capacity>0&&a.capacity%4096===0&&a.bytes>0&&a.bytes%4===0&&a.bytes<=a.capacity);region({space:'private',offset:a.offset},a.capacity);assert(Number.isInteger(a.descriptors)&&a.descriptors>=1&&a.descriptors<=32);out[1]=1;out[2]=a.offset;out[5]=a.capacity;out[6]=a.bytes;out[7]=a.descriptors;
  }else keys(a,['op']);operations.push(out);
 }
 function actions(a){assert(Array.isArray(a)&&a.length<=32,'Invalid local action list');const start=operations.length;a.forEach(action);return [start,a.length];}
 const guards=plan.guards||[];assert(Array.isArray(guards)&&guards.length<=64);guards.forEach(condition);
 const init=actions(plan.initialize||[]);assert(Array.isArray(plan.rules||[])&&(plan.rules||[]).length<=32);
 for(const r of plan.rules||[]){keys(r,['when','once','actions']);assert(r.once===undefined||typeof r.once==='boolean');assert(Array.isArray(r.when)&&r.when.length<=64&&r.when.length>0);const c=conditions.length;r.when.forEach(condition);const [o,n]=actions(r.actions);assert(n>0);rules.push([c,r.when.length,o,n,r.once===false?0:1,0,0,0]);}
 const stop=actions(plan.stop||[]);assert(stop[1]<=16&&operations.slice(stop[0]).every(o=>[1,2,4].includes(o[0])),'Stop instructions may only save or update owned data');assert(operations.slice(stop[0]).filter(o=>o[0]===4).reduce((n,o)=>n+o[5],0)<=65536,'Stop file work exceeds one frame');assert(conditions.length<=256&&operations.length<=256&&stringBytes<=8192,'Local controller is too large');
 const atConditions=64,atRules=atConditions+conditions.length*24,atOps=atRules+rules.length*32,atStrings=atOps+operations.length*40,bytes=atStrings+stringBytes,runtimeOffset=(bytes+15)&~15,dataBytes=Math.ceil((runtimeOffset+4096)/4096)*4096,buffer=Buffer.alloc(bytes);
 const h=[0x434c5a49,1,bytes,runtimeOffset,guards.length,init[1],rules.length,stop[1],atConditions,atOps+init[0]*40,atRules,atOps+stop[0]*40,atOps,operations.length,atStrings,stringBytes];h.forEach((n,i)=>buffer.writeUInt32LE(n,i*4));
 conditions.forEach((c,i)=>c.forEach((n,k)=>buffer.writeUInt32LE(n,atConditions+i*24+k*4)));rules.forEach((r,i)=>{r=[atConditions+r[0]*24,r[1],atOps+r[2]*40,...r.slice(3)];r.forEach((n,k)=>buffer.writeUInt32LE(n,atRules+i*32+k*4));});operations.forEach((o,i)=>{o=[...o];if(o[0]===3||o[0]===4)o[7]+=atStrings;o.forEach((n,k)=>buffer.writeUInt32LE(n,atOps+i*40+k*4));});Buffer.concat(strings).copy(buffer,atStrings);
 const audio=operations.some(o=>o[0]===6||o[0]===7);assert(operations.filter(o=>o[0]===6).length<=1&&operations.filter(o=>o[0]===7).length<=1,'Only one owned AudioOut queue per local controller');
 for(const o of operations){if([1,2,3,4,6].includes(o[0])&&o[1]===1)assert(o[2]+(o[0]===1?4:o[5])<=privateBytes,'Local instruction exceeds actual private memory');if(o[0]===2&&o[3]===1)assert(o[4]+o[5]<=privateBytes,'Local copy exceeds actual private memory');}
 for(const c of conditions)if(c[0]===1)assert(c[1]+4<=privateBytes,'Local condition exceeds actual private memory');
 return {bytes:buffer,dataBytes,capabilities:audio?['audio-out']:[],rules:rules.length,operations:operations.length};
}
module.exports={compile};
