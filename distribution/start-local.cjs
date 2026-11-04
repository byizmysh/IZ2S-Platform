// Start the background PC server. start.cjs owns reuse/health checks and browser opening.
const fs = require('node:fs'), path = require('node:path'), {spawn} = require('node:child_process');
async function launch(root, {node = process.execPath, args = [], env = process.env} = {}) {
  const state = path.join(root,'data/state'); fs.mkdirSync(state,{recursive:true});
  const log = fs.openSync(path.join(state,'startup.log'),'a',0o600);
  try {
    return await new Promise((resolve,reject)=>{
      const child = spawn(node,[path.join(root,'start.cjs'),...args],{cwd:root,env,detached:true,windowsHide:true,stdio:['ignore',log,log]});
      child.once('error',reject);child.once('spawn',()=>{child.unref();resolve({pid:child.pid});});
    });
  } finally {fs.closeSync(log);}
}
if(require.main===module)launch(path.resolve(__dirname,'..'),{args:process.argv.slice(2)})
  .then(()=>console.log('IZ2S startup requested. If no window opens, see data/state/startup.log.'))
  .catch(error=>{console.error(error.message);process.exitCode=1;});
module.exports={launch};
