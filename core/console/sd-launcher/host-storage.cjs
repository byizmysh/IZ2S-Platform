'use strict';
// Observe the card itself. A PC receipt never substitutes for card presence.
async function inventory({directory,identifyDigest}){
 const path=require('node:path'),a=await require('../../filesystem/ouncecake/engine/session.cjs').attachGame(path.join(directory,'fs-rpc'));let storage,retained=false;
 try{storage=await require('./storage.cjs').create(a);return await require('./install.cjs').inventory(storage,{identities:true,identifyDigest});}
 catch(error){if(!storage||storage.uncertain)error.resourcesRetained=true;retained=!!error.resourcesRetained;throw error;}
 finally{if(storage&&!storage.uncertain&&!retained)await a.close();}
}
function verify(job,result){
 const assert=require('node:assert/strict');
 assert(result?.id===job.packageId,'SD result identity differs');
 if(job.operation==='remove')assert(result.removed,'SD removal was not verified');
 else assert(result.readbackVerified,'SD installation was not verified');
 if(result.assetManifest!==undefined){assert(Array.isArray(result.assets)&&result.assets.every(a=>a.readbackVerified===true),'SD companion installation was not verified');assert.deepEqual(require('./assets.cjs').binding(result.assets.map(a=>({...a,path:a.relativePath}))),result.assetManifest,'SD companion receipts differ from the application');}
 return result;
}
function matchesPublication(job,rows,{requireIdentity=true}={}){
 const card=rows.find(row=>row.id===job.packageId),result=job.result;
 if(job.operation==='remove')return !card;
 if(!requireIdentity&&!result?.headerSha256)return !!card;
 return !!card?.headerSha256&&card.headerSha256===result?.headerSha256&&card.bytes===result.bytes;
}
function catalogMatches(job,app){
 const crc=job.result?.headerCrc;
 return !!app&&(!Number.isInteger(crc)||app.headerCrc===crc);
}
function hiddenByMixedMenu(state,rows,id){
 return state.count===15&&state.applications.some(app=>app.source==='session')&&
  rows.some(row=>row.id===id&&row.headerValid===true);
}
module.exports={inventory,verify,matchesPublication,catalogMatches,hiddenByMixedMenu};
