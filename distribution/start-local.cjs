// Start the background PC server. start.cjs owns reuse/health checks and browser opening.
const fs = require('node:fs'), path = require('node:path'), {spawn} = require('node:child_process');
async function launch(root, {node = process.execPath, args = [], env = process.env,
  timeout = 75000, fetcher = fetch} = {}) {
  const state = path.join(root,'data/state'); fs.mkdirSync(state,{recursive:true});
  const log = fs.openSync(path.join(state,'startup.log'),'a',0o600);
  try {
    const child = await new Promise((resolve,reject)=>{
      const child = spawn(node,[path.join(root,'start.cjs'),...args],{cwd:root,env,detached:true,windowsHide:true,stdio:['ignore',log,log]});
      child.once('error',reject);child.once('spawn',()=>{child.unref();resolve(child);});
    });
    let failed;
    child.once('exit',(code,signal)=>{if(code!==0)failed=signal||code;});
    const preview=args.includes('--preview'),file=path.join(state,preview?'preview-window.json':'window.json');
    const deadline=Date.now()+timeout;
    while(Date.now()<deadline) {
      try {
        const saved=JSON.parse(await fs.promises.readFile(file,'utf8')),url=new URL(saved.url);
        if(url.hostname!=='127.0.0.1'||saved.preview!==preview)throw Error('Different panel instance');
        const response=await fetcher(url.origin+'/health',{signal:AbortSignal.timeout(1500)});
        const health=await response.json();
        if(response.ok&&!health.closing&&health.pid===saved.pid&&health.edition==='0.25'&&health.preview===preview)
          return {pid:saved.pid,url:saved.url};
      } catch {}
      if(failed!==undefined)break;
      await new Promise(resolve=>setTimeout(resolve,200));
    }
    const tail=(await fs.promises.readFile(path.join(state,'startup.log'),'utf8')).trim().split(/\r?\n/).slice(-1)[0];
    throw Error('The panel did not start: '+(tail||failed||'startup timed out')+'. See '+path.join(state,'startup.log'));
  } finally {fs.closeSync(log);}
}
if(require.main===module)launch(path.resolve(__dirname,'..'),{args:process.argv.slice(2)})
  .then(()=>console.log('IZ2S panel is ready.'))
  .catch(error=>{console.error(error.message);process.exitCode=1;});
module.exports={launch};
