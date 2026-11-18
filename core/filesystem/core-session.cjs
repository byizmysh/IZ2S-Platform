'use strict';
const fs=require('node:fs/promises'),path=require('node:path'),net=require('node:net');
const read=file=>fs.readFile(file,'utf8').then(JSON.parse);

// IZ2S's installer retains a real FS endpoint until SmileBASIC exits. Its proof
// lives in result.ro, unlike the older launcher's fs-execution-current.json.
// Never scan neighbouring releases or reconnect to historical run folders.
async function find(root,{out=process.env.IZ2S_CORE_OUT||path.join(root,'data/core'),preflight=require('./games.cjs').preflight}={}){
  const runtime=require('../../engines/iz2s-core/runtime.cjs');
  const latest=await read(path.join(runtime.journal(out),'latest.json')).catch(e=>{
    if(e.code==='ENOENT')return null;throw e;
  });
  if(!latest?.run||latest.kind!=='launch'||['stopped','failed','interrupted'].includes(latest.state))return null;
  const parent=await fs.realpath(runtime.runs(out));
  const run=await fs.realpath(latest.run);
  if(path.dirname(run)!==parent||!/^switch2-[A-Za-z0-9]+$/.test(path.basename(run)))return null;
  const [result,connection,current,proof]=await Promise.all([
    read(path.join(run,'result.json')),read(path.join(run,'connection.json')),
    read(path.join(run,'switch2/current-acquisition.json')),
    read(path.join(run,'switch2/ro/root/current-root.json')),
  ]);
  const session=connection.addresses?.session,ro=result.ro;
  if(session!==path.basename(run)||current.session!==session||proof.session!==session||
     !result.verified||result.rpc?.state!=='listening'||!ro?.verified||!ro.restored||
     !ro.cleanup?.deferred||!ro.retainedEndpoint?.ready||ro.session!==session||
     ro.retainedEndpoint.socketPath!==current.socketPath)return null;
  const status=await preflight(run);
  if(!status.ready)return null;
  // Prefer the peer that actually supplied the bootstrap, not a default IP.
  const saved=await read(path.join(runtime.logs(out),'pc-link/connection.json')).catch(e=>{
    if(e.code==='ENOENT')return {};throw e;
  });
  const host=connection.consoleAddress||saved.consoleIp;
  if(!net.isIPv4(host||''))return null;
  return {id:latest.id,session,run,host,port:2122,source:'core-fs-rpc',
    identity:session+':'+current.socketPath};
}
module.exports={find};
