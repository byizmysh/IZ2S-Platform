'use strict';
// One entry point for first setup and subsequent starts of the portable ZIP.
const fs = require('node:fs/promises'), path = require('node:path');
const {spawn} = require('node:child_process');
const source = path.resolve(__dirname,'..');
const sleep = ms => new Promise(resolve => setTimeout(resolve,ms));
async function setupLock(root,{timeout=900000}={}) {
  const file=path.join(root,'data/state/windows-setup.lock');
  await fs.mkdir(path.dirname(file),{recursive:true});
  const deadline=Date.now()+timeout;
  let announced=false;
  for (;;) {
    try {
      const handle=await fs.open(file,'wx');
      await handle.writeFile(JSON.stringify({pid:process.pid}));await handle.close();
      return async()=>{await fs.rm(file,{force:true});};
    } catch(error) {if(error.code!=='EEXIST')throw error;}
    try {
      const owner=JSON.parse(await fs.readFile(file,'utf8'));
      if (!Number.isSafeInteger(owner.pid) || owner.pid<1) throw Error('Invalid setup lock. Close setup and remove '+file);
      try {process.kill(owner.pid,0);}
      catch(error) {if(error.code!=='ESRCH')throw error;await fs.rm(file,{force:true});continue;}
    } catch(error) {
      if(error.code==='ENOENT')continue;
      // A newly created lock may not have its PID written yet.
      if(!(error instanceof SyntaxError))throw error;
      const stat=await fs.stat(file).catch(()=>null);
      if(stat&&Date.now()-stat.mtimeMs>60000)throw Error('Incomplete setup lock. Close setup and remove '+file);
    }
    if(Date.now()>deadline)throw Error('Another setup is still running. Wait for it to finish, then open Start.cmd again.');
    if(!announced){console.log('Waiting for the other setup window…');announced=true;}
    await sleep(500);
  }
}
async function ready(root,{execute=require('./install.cjs').run}={}) {
  try {
    const installed=JSON.parse(await fs.readFile(path.join(root,'data/state/installation.json')));
    const version=JSON.parse(await fs.readFile(path.join(root,'package.json'))).version;
    if(installed.status!=='complete'||installed.version!==version||installed.platform!=='win32')return false;
    await fs.access(path.join(root,'data/runtime/node/node.exe'));
    // This also detects a virtual environment invalidated by moving the folder.
    await execute(path.join(root,'data/runtime/venv/Scripts/python.exe'),['-c','from PIL import Image, ImageDraw, ImageFont, ImageOps'],{stdio:'ignore',timeout:15000});
    return true;
  } catch {return false;}
}
function loggedRun(log) {
  return (program,args,options={})=>new Promise((resolve,reject)=>{
    const quiet=options.stdio==='ignore';
    const child=spawn(program,args,{...options,windowsHide:true,stdio:quiet?'ignore':['ignore','pipe','pipe']});
    for(const [stream,target]of [[child.stdout,process.stdout],[child.stderr,process.stderr]])
      stream?.on('data',bytes=>{target.write(bytes);log.write(bytes);});
    child.once('error',reject);
    child.once('close',(code,signal)=>code===0?resolve():reject(Error(path.basename(program)+' failed ('+(signal||code)+')')));
  });
}
async function start({root=source,args=process.argv.slice(2),platform=process.platform,
  install=require('./install.cjs').install,launch=require('./start-local.cjs').launch,
  isReady=ready,lock=setupLock,execute}={}) {
  if(platform!=='win32'||process.arch!=='x64')throw Error('Open Start.cmd on Windows x64.');
  const release=await lock(root);
  try {
    if(!await isReady(root)) {
      console.log('First launch: preparing Node.js, Python, Pillow and the required host tools. Internet access is required.');
      const log= require('node:fs').createWriteStream(path.join(root,'data/state/setup.log'),{flags:'a'});
      try {
        await install({source:root,portable:true,nodeHome:path.dirname(process.execPath),
          createShortcut:false,launch:false,execute:execute||loggedRun(log)});
      } catch(error) {log.write('\n'+error.stack+'\n');throw error;}
      finally {await new Promise(resolve=>log.end(resolve));}
    }
  } finally {await release();}
  if(args.includes('--setup-only'))return {ready:true};
  const node=path.join(root,'data/runtime/node/node.exe');
  return launch(root,{node,args:args.filter(arg=>arg!=='--setup-only')});
}
if(require.main===module)start().catch(error=>{
  console.error('IZ2S: '+error.message);
  console.error('Open Start.cmd again to retry. Setup log: data\\state\\setup.log');
  process.exitCode=1;
});
module.exports={start,ready,setupLock};
