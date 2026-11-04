'use strict';
// Session modules keep their native resources in their owner process. Control
// them there rather than terminating that process from the desktop panel.
const {AsyncLocalStorage}=require('node:async_hooks'),http=require('node:http'),crypto=require('node:crypto'),path=require('node:path');
const {readJson,saveJson}=require('./session/ledger.cjs'),scope=new AsyncLocalStorage();
const ownerFile=(root,origin)=>path.join(root,'data/state/workspace-owners',crypto.createHash('sha256').update(origin).digest('hex')+'.json');
async function request(controlUrl,action,id,url) {
  const target=require('./workspace.cjs').localUrl(controlUrl);
  if(!/^#[a-f0-9]{48}$/.test(target.hash))throw Error('Invalid session module control');
  let response;
  try{response=await fetch(target.origin+'/'+action,{method:'POST',headers:{origin:target.origin,'x-workspace-key':target.hash.slice(1),'content-type':'application/json'},body:JSON.stringify({id,url}),signal:AbortSignal.timeout(150000)});}
  catch(error){if(error.cause?.code==='ECONNREFUSED')throw Error('The module session has ended. Start a console session first.');throw error;}
  const result=await response.json();if(!response.ok||!result.ok)throw Error(result.error||'Module control was not confirmed');return result;
}
async function attach(root,url) {
  const target=require('./workspace.cjs').localUrl(url),owner=await readJson(ownerFile(root,target.origin));
  if(!owner)throw Error('This module does not expose a session control. Open it from its module tab.');
  try{process.kill(owner.ownerPid,0);}catch{throw Error('The module session has ended.');}
  return request(owner.controlUrl,'attach',owner.moduleId,target.href);
}
function create({root,start,stop}) {
  const key=crypto.randomBytes(24).toString('hex'),origins=new Map();
  let server,controlUrl,opening,closing=false,queue=Promise.resolve();
  async function mark(id,state) {
    const workspace=require('./workspace.cjs');
    for(const row of await workspace.list(root))if(row.moduleId===id&&row.controlUrl===controlUrl)
      await saveJson(path.join(root,'data/state/workspace-tabs',row.id+'.json'),{...row,state,updatedAt:new Date().toISOString()});
  }
  async function register(id,title,url) {
    const target=require('./workspace.cjs').localUrl(url);
    if(!origins.get(id)?.has(target.origin))throw Error('Interface does not belong to this session module');
    return require('./workspace.cjs').register(root,{id:'module-'+id,moduleId:id,title,url:target.href,kind:'module',ownerPid:process.pid,controlUrl});
  }
  async function listen(id,title,origin) {
    if(closing)throw Error('The console session is stopping');
    if(!opening)opening=(async()=>{
      server=http.createServer(async(req,res)=>{
        const reply=(code,value)=>{res.writeHead(code,{'content-type':'application/json','cache-control':'no-store'});res.end(JSON.stringify(value));};
        try{
          const expected=new URL(controlUrl).origin;
          if(req.method!=='POST'||req.headers.host!==new URL(expected).host||req.headers.origin!==expected||req.headers['x-workspace-key']!==key)return reply(403,{error:'Use the owned module control'});
          let size=0,parts=[];for await(const part of req){size+=part.length;if(size>2048)throw Error('Module command is too large');parts.push(part);}
          const body=JSON.parse(Buffer.concat(parts));if(!origins.has(body.id))throw Error('Unknown session module');
          const task=queue.catch(()=>{}).then(async()=>{
            if(closing)throw Error('The console session is stopping');
            if(req.url==='/stop'){try{await stop(body.id);}catch(error){await mark(body.id,'retained');throw error;}await mark(body.id,'stopped');}
            else if(req.url==='/start'){await start(body.id);}
            else if(req.url==='/attach'){const owner=origins.get(body.id);const tab=await register(body.id,owner.title,body.url);return {ok:true,tab};}
            else throw Error('Unknown module command');
            const tab=(await require('./workspace.cjs').list(root)).find(row=>row.moduleId===body.id&&row.controlUrl===controlUrl);
            return {ok:true,tab};
          });queue=task;reply(200,await task);
        }catch(error){reply(400,{ok:false,error:error.message});}
      });
      await new Promise((resolve,reject)=>{server.once('error',reject);server.listen(0,'127.0.0.1',resolve);});
      server.unref();controlUrl='http://127.0.0.1:'+server.address().port+'/#'+key;
    })();
    await opening;
    let row=origins.get(id);if(!row){row=new Set();row.title=title;origins.set(id,row);}row.add(origin);
    await saveJson(ownerFile(root,origin),{moduleId:id,title,ownerPid:process.pid,controlUrl});
  }
  return {
    run(id,title,callback){return scope.run({root,id,title,listen:origin=>listen(id,title,origin),open:url=>register(id,title,url),
      accepts:url=>origins.get(id)?.has(require('./workspace.cjs').localUrl(url).origin)===true},callback);},
    mark,
    async close({confirmed=true}={}){closing=true;await queue.catch(()=>{});if(confirmed)for(const id of origins.keys())await mark(id,'stopped');if(server){server.closeIdleConnections();await new Promise(resolve=>server.close(resolve));}},
    drain:()=>queue.catch(()=>{}),
  };
}
module.exports={create,current:()=>scope.getStore(),request,attach};
