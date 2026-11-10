'use strict';
const fs=require('node:fs/promises'),path=require('node:path');
const root=path.resolve(__dirname,'../..');
async function main(session){
 if(!/^launcher-[A-Za-z0-9]+$/.test(session||''))throw Error('Expected session ID');
 const directory=path.join(root,'data/sessions',session),boundary=await require('../../core/console/sd-launcher/boundary.cjs').acquire(directory),a=await require('../../core/filesystem/ouncecake/engine/session.cjs').attachGame(path.join(directory,'fs-rpc'));
 const storage=await require('../../core/console/sd-launcher/storage.cjs').create(a);await require('../../core/console/sd-launcher/install.cjs').ensure(storage,'iz-sd-launcher');
 const nonce=require('node:crypto').randomInt(1,0xffffffff),supervisor=await require('../../core/console/sd-launcher/bootstrap.cjs').deploy({directory,probe:true,probeNonce:nonce,attachment:a,displayToken:boundary.token});
 let state;const deadline=Date.now()+10000;do{state=await supervisor.status();if(state.state===3||state.state===4)break;await new Promise(r=>setTimeout(r,100));}while(Date.now()<deadline);
 supervisor.report.observed=state;await supervisor.save();console.log(JSON.stringify(state));if(state.state!==3||state.probe_result)throw Error('Native SD write/read/rename/delete probe failed');await supervisor.close();await a.close();await boundary.release();
 await fs.writeFile(path.join(directory,'sd-native-probe-result.json'),JSON.stringify({passed:true,nonce,...state,at:new Date().toISOString()},null,2)+'\n');
}
if(require.main===module)main(process.argv[2]).catch(e=>{console.error(e);process.exitCode=1;});module.exports={main};
