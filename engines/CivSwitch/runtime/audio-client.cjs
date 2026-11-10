'use strict';
// Civ7 SDK functions are resolved by their exact exported signatures. SB4
// addresses and instruction fingerprints are never invoked in this process.
const symbols={
 InitializeAudioOutParameter:'_ZN2nn5audio27InitializeAudioOutParameterEPNS0_17AudioOutParameterE',
 OpenDefaultAudioOut:'_ZN2nn5audio19OpenDefaultAudioOutEPNS0_8AudioOutERKNS0_17AudioOutParameterE',
 GetAudioOutSampleRate:'_ZN2nn5audio21GetAudioOutSampleRateEPKNS0_8AudioOutE',
 GetAudioOutChannelCount:'_ZN2nn5audio23GetAudioOutChannelCountEPKNS0_8AudioOutE',
 GetAudioOutSampleFormat:'_ZN2nn5audio23GetAudioOutSampleFormatEPKNS0_8AudioOutE',
 SetAudioOutBufferInfo:'_ZN2nn5audio21SetAudioOutBufferInfoEPNS0_14AudioOutBufferEPvmm',
 AppendAudioOutBuffer:'_ZN2nn5audio20AppendAudioOutBufferEPNS0_8AudioOutEPNS0_14AudioOutBufferE',
 StartAudioOut:'_ZN2nn5audio13StartAudioOutEPNS0_8AudioOutE',StopAudioOut:'_ZN2nn5audio12StopAudioOutEPNS0_8AudioOutE',
 FlushAudioOutBuffers:'_ZN2nn5audio20FlushAudioOutBuffersEPNS0_8AudioOutE',
 GetReleasedAudioOutBuffer:'_ZN2nn5audio25GetReleasedAudioOutBufferEPNS0_8AudioOutE',
 GetAudioOutBufferCount:'_ZN2nn5audio22GetAudioOutBufferCountEPKNS0_8AudioOutE',CloseAudioOut:'_ZN2nn5audio13CloseAudioOutEPNS0_8AudioOutE',
};
async function create(rpc,client,definitions,bytes){
 if(!Number.isInteger(bytes)||bytes<4096||bytes>1048576||bytes%4096)throw Error('Invalid Civ7 native allocation size');
 const functions={};for(const name of Object.keys(definitions)){
  if(!Object.hasOwn(symbols,name))throw Error('SDK service needs Civ7 adaptation: '+name);
  functions[name]=(await client.lookup(symbols[name],'sdk')).address;
 }
 const allocation=await rpc.call(5732112,[rpc.output,4096,bytes]);if(allocation.result!==0n)throw Error('Civ7 AudioOut allocation failed');
 const base=Number(allocation.output.readBigUInt64LE());if(!Number.isSafeInteger(base)||base<4096||base%4096)throw Error('Invalid Civ7 AudioOut allocation');
 await rpc.initializeOwned(base,bytes);
 const bounds=(offset,length)=>{if(!Number.isSafeInteger(offset)||!Number.isSafeInteger(length)||offset<0||length<0||offset+length>bytes)throw Error('Native buffer bounds');};
 return {rpc,base,bytes,functions,async call(name,args=[]){
  if(!Object.hasOwn(functions,name)||args.length>4)throw Error('Unknown or invalid Civ7 AudioOut operation');
  return client.call(symbols[name],args,'sdk');
 },async result(name,args=[]){return Number(BigInt.asUintN(32,await this.call(name,args)));},
 async read(offset,length){bounds(offset,length);return rpc.read(base+offset,length);},
 async write(offset,data){bounds(offset,data.length);return rpc.write(base+offset,data);}};
}
module.exports={create,symbols};
