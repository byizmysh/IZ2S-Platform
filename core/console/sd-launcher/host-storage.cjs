'use strict';
// Observe the card itself. A PC receipt never substitutes for card presence.
async function inventory({directory}){
 const path=require('node:path'),a=await require('../../filesystem/ouncecake/engine/session.cjs').attachGame(path.join(directory,'fs-rpc'));let storage;
 try{storage=await require('./storage.cjs').create(a);return await require('./install.cjs').inventory(storage,{identities:true});}
 catch(error){if(!storage||storage.uncertain)error.resourcesRetained=true;throw error;}
 finally{if(storage&&!storage.uncertain)await a.close();}
}
function verify(job,result){
 const assert=require('node:assert/strict');
 assert(result?.id===job.packageId,'SD result identity differs');
 if(job.operation==='remove')assert(result.removed,'SD removal was not verified');
 else assert(result.readbackVerified,'SD installation was not verified');
 if(result.assetManifest!==undefined){assert(Array.isArray(result.assets)&&result.assets.every(a=>a.readbackVerified===true),'SD companion installation was not verified');assert.deepEqual(require('./assets.cjs').binding(result.assets.map(a=>({...a,path:a.relativePath}))),result.assetManifest,'SD companion receipts differ from the application');}
 return result;
}
module.exports={inventory,verify};
