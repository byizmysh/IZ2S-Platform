'use strict';
// Typed local RPC only. No callback, raw packet or JavaScript execution route.
const assert=require('node:assert/strict');
const MAX=8*1024*1024;
function bytes(text,max=MAX){assert(typeof text==='string'&&text.length<=Math.ceil(max/3)*4&&text.length%4===0,'Invalid gateway binary data');const data=Buffer.from(text,'base64');assert(data.length<=max&&data.toString('base64')===text,'Invalid gateway binary data');return data;}
function list(value){assert(Array.isArray(value)&&value.length<=8,'Invalid gateway arguments');return value;}
function result(row){return {result:row.result.toString(),...(row.output?{output:row.output.toString('base64')}:{})};}
async function invoke(api,row,{raw=false}={}){
 assert(row&&typeof row.command==='string','Invalid gateway RPC');
 switch(row.command){
  case 'read':assert(Number.isInteger(row.length)&&row.length>=0&&row.length<=MAX);return {data:(await api.read(row.address,row.length)).toString('base64')};
  case 'read-many':assert(Array.isArray(row.spans)&&row.spans.length<=8&&row.spans.reduce((n,s)=>n+s.length,0)<=32768);return {parts:(await api.readMany(row.spans)).map(b=>b.toString('base64'))};
  case 'query':return api.query(row.address);
  case 'fill':assert(!raw);return result(await api.fill(row.address,row.value,row.length));
  case 'copy':assert(!raw);return result(await api.copy(row.destination,row.source,row.length));
  case 'write':await api.write(row.address,bytes(row.data));return {written:true};
  case 'call':return result(await (raw?api.call(row.offset,...list(row.args)):api.call(row.offset,list(row.args))));
  case 'call-address':return result(await (raw?api.callAddress(row.address,...list(row.args)):api.callAddress(row.address,list(row.args))));
  case 'call-extended':assert(!raw,'Raw extended calls are unavailable');return result(await api.callExtended(row.address,list(row.args)));
  case 'haptics':{assert(!raw);const r=await api.haptics({handles:row.handles,steps:row.steps});return {result:r.result.toString(),nativeResult:r.nativeResult,completedSteps:r.completedSteps,actual:r.actual.toString('base64'),afterStop:r.afterStop.toString('base64')};}
  case 'udp':{assert(!raw);const r=await api.udp(row.destination,bytes(row.data,1024),{timeout:row.timeout});return {result:r.result.toString(),nativeResult:r.nativeResult,polls:r.polls,socketClosed:r.socketClosed,data:r.data.toString('base64')};}
  case 'read-file':{assert(!raw);const r=await api.readFile(row.path,row.offset,row.length);return {result:r.result.toString(),nativeResult:r.nativeResult,fileClosed:r.fileClosed,size:r.size.toString(),offset:r.offset.toString(),data:r.data.toString('base64')};}
  case 'pair':{assert(Array.isArray(row.calls)&&row.calls.length>=1&&row.calls.length<=2);const reply=raw?await api.exchange(row.calls):await api.pair(row.calls);return {results:reply.results.map(String),output:reply.output.toString('base64')};}
  case 'write-call':return {...result(await api.writeCall(row.address,bytes(row.data,32768),row.functionAddress,list(row.args))),written:true};
  case 'verify-function':assert(typeof row.hex==='string'&&/^(?:[0-9a-f]{2}){4,256}$/i.test(row.hex));if(raw)await api.verifyFunction(row.address,Buffer.from(row.hex,'hex'));else await api.request(row);return {verified:true};
  default:throw Object.assign(Error('Unsupported gateway RPC command'),{code:'IZ2S_GATEWAY_COMMAND',nativeRequestSent:false,requestReplayed:false});
 }
}
module.exports={MAX,invoke};
