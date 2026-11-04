// Offline panel smoke test only. It never requests a console/input/firmware action.
const fs=require('node:fs/promises'),path=require('node:path'),{spawn}=require('node:child_process');
async function check(root, {node=process.execPath}={}) {
  const state=path.join(root,'data/state/preview-window.json');
  try{await fs.stat(state);throw Error('Use an isolated installation without an existing preview');}catch(error){if(error.code!=='ENOENT')throw error;}
  const child=spawn(node,[path.join(root,'start.cjs'),'--preview','--no-open'],{cwd:root,stdio:['ignore','ignore','pipe']});
  let stderr='',failure;child.stderr.on('data',b=>{stderr=(stderr+b).slice(-2000);});child.once('error',error=>{failure=error;});
  try {
    let window;
    for(let i=0;i<150;i++){
      if(failure)throw failure;if(child.exitCode!==null)throw Error('Preview exited before ready');
      try{window=JSON.parse(await fs.readFile(state));if(window.pid===child.pid)break;}catch(error){if(error.code!=='ENOENT'&&!(error instanceof SyntaxError))throw error;}
      await new Promise(resolve=>setTimeout(resolve,100));
    }
    if(window?.pid!==child.pid)throw Error('Preview startup timed out');
    const url=new URL(window.url),health=await fetch(url.origin+'/health').then(r=>r.json());
    if(!health.preview||health.pid!==child.pid)throw Error('Wrong preview owner');
    const page=await fetch(url.origin+'/').then(r=>r.text());if(!page.includes('IZ2S'))throw Error('Panel HTML is missing');
    const response=await fetch(url.origin+'/api/state',{headers:{'x-iz-key':url.hash.slice(1)}});
    if(!response.ok)throw Error('Preview state request failed');
    console.log(JSON.stringify({preview:true,platform:process.platform,architecture:process.arch,health:true,page:true,authenticatedState:true,deviceActions:false}));
  } finally {
    // Preview contains no native session. Windows process termination here is
    // deliberately not evidence of qualified native-session cleanup.
    if(child.exitCode===null){child.kill('SIGTERM');await Promise.race([new Promise(resolve=>child.once('close',resolve)),new Promise(resolve=>{const timer=setTimeout(()=>{child.kill('SIGKILL');resolve();},5000);timer.unref();})]);}
  }
}
if(require.main===module)check(path.resolve(process.argv[2]||path.join(__dirname,'..'))).catch(error=>{console.error(error.message);process.exitCode=1;});
module.exports={check};
