'use strict';
// Losing a transport receipt is not permission to free native allocations.
// Retention protects resources; it does not claim that a bounded frame batch
// is an autonomous console renderer.
const policy=require('../../../core/session/offline-policy.cjs');
function retain(error,{client,rpc}={}){
 return !!(client?.uncertain||rpc?.uncertain||error?.uncertain||error?.resourcesRetained||error?.restartRequired||policy.isTransportLoss(error)||/CivSwitch (?:reply timed out|connection closed before reply)/i.test(error?.message||''));
}
async function finish({retained,cleanup,save,report}){
 if(!retained)await cleanup();
 report.completed=new Date().toISOString();report.resourcesRetained=true;await save();
}
module.exports={retain,finish};
