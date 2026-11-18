'use strict';
// Framework-owned controller. App services receive only fixed-epoch RPC views;
// cancellation and return attestation remain outside their context.rpc object.
const assert=require('node:assert/strict'),path=require('node:path');
const channels=new WeakMap();
function isChannel(channel){return channels.has(channel);}
function isReturned(channel,proof){return !!proof&&proof.returned===true&&channels.get(channel)?.proof===proof;}
function ownsRPC(channel,rpc){const identity=channels.get(channel);if(!identity||identity.proof)return false;const scopes=require('../../services/app-rpc-scope.cjs');for(const view of identity.views)if(rpc===view||scopes.isRPCFor(rpc,view))return true;return false;}
function decode(text,length){assert(typeof text==='string');const bytes=Buffer.from(text,'base64');assert(bytes.length===length&&bytes.toString('base64')===text,'Invalid application observation');return bytes;}
async function open({directory,supervisor,targets,wideCalls=true,sdkServices=true,clock=Date.now,pause=ms=>new Promise(resolve=>setTimeout(resolve,ms)),timeoutMs=15000}){
 directory=path.resolve(directory);const client=await require('./gateway-client.cjs').connect(directory),views=new Set(),session=path.basename(directory);let epoch=0,closing,returned=false,lastReturnProof;
 function view(capability){const source=client.app(capability),rpc={...source,a:Object.freeze({...source.a,session})};Object.defineProperty(rpc,'uncertain',{get:()=>source.uncertain,enumerable:true});const fixed=Object.freeze(rpc);views.add(fixed);return fixed;}
 try{epoch=(await client.control('claim-native',{supervisor})).epoch;const rpc=view(await client.control('commit-native',{epoch,binding:{supervisor,...(targets?{targets}:{}),frameMode:1,sdkServices},wideCalls}));
  const report={format:'iz2s.application-channel/1',session,epoch,state:'ready',generationGuarded:true,nativeArgumentLimit:rpc.nativeArgumentLimit,nativeOwnershipReleased:false,resourcesReleased:false};
  const identity={proof:null,views};
  function retainFailure(error){if([...views].some(view=>view.uncertain))Object.assign(error,{resourcesRetained:true,requestReplayed:false});return error;}
  async function synchronize(){if(returned)return lastReturnProof;const proof=await client.control('synchronize-native',{epoch});if(proof.returned){returned=true;lastReturnProof=Object.freeze(proof);identity.proof=lastReturnProof;for(const v of views)v.revoke();report.state='returned';}report.returned=proof.returned;return returned?lastReturnProof:proof;}
  function close({errorCode=0}={}){if(!closing){for(const v of views)v.revoke();closing=(async()=>{if(returned)return synchronize();report.state='closing';await client.cancel(epoch,errorCode);const started=clock();for(;;){const proof=await synchronize();if(proof.returned)return proof;if(clock()-started>=timeoutMs)throw Object.assign(Error('Native application return remains unconfirmed'),{resourcesRetained:true,requestReplayed:false});await pause(25);}})().catch(error=>{report.state='retained';report.error=error.message;throw retainFailure(error);});}return closing;}
  // This observer stays with the framework. Services never receive controller
  // credentials, a raw mutation route, or authority over the next generation.
  const observe=request=>client.control('observe',{epoch,request});
  const observer=Object.freeze({a:rpc.a,
   async read(address,length){assert(Number.isSafeInteger(length)&&length>=0&&length<=64*1024*1024);const parts=[];for(let at=0;at<length;at+=8*1024*1024){const n=Math.min(8*1024*1024,length-at),row=await observe({command:'read',address:address+at,length:n});parts.push(decode(row.data,n));}return Buffer.concat(parts);},
   async readMany(spans){const row=await observe({command:'read-many',spans});assert(Array.isArray(row.parts)&&row.parts.length===spans.length);return row.parts.map((text,i)=>decode(text,spans[i].length));},
   async query(address){const row=await observe({command:'query',address});assert(/^(0|[1-9][0-9]{0,19})$/.test(row.address)&&/^(0|[1-9][0-9]{0,19})$/.test(row.size)&&Number.isInteger(row.permissions));return {...row,address:BigInt(row.address),size:BigInt(row.size)};}
  });
  const channel=Object.freeze({rpc,observer,epoch,report,protectedRanges:Object.freeze([Object.freeze({base:supervisor.base,bytes:supervisor.bytes})]),async participant(){assert(!returned&&!closing,'Application channel is closing');return view(await client.control('participant',{epoch}));},async delegate({id,controls,gpu,commands=gpu?.commands?.[0]}){assert(!returned&&!closing,'Application channel is closing');const ticket=await client.control('delegate-native-frames',{epoch,frames:{id,controls,commands}});report.state='running';return ticket;},requestReturn:()=>returned?Promise.resolve(lastReturnProof):client.cancel(epoch),synchronize,close});
  channels.set(channel,identity);return channel;
 }catch(error){if(epoch&&!error.resourcesRetained){try{if(!error.nativeGenerationReturned)await client.cancel(epoch);const proof=await client.control('synchronize-native',{epoch});if(!proof.returned)error.resourcesRetained=true;}catch(cleanup){error.resourcesRetained=true;error.cleanupError=cleanup.message;}}throw error;}
}
module.exports={open,isChannel,isReturned,ownsRPC};
