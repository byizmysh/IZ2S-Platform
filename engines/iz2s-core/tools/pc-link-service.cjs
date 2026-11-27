'use strict';
const fs=require('node:fs/promises'),path=require('node:path'),net=require('node:net');
const os=require('node:os');
const {randomUUID}=require('node:crypto');
const {spawn,execFile}=require('node:child_process');
const {promisify}=require('node:util');
const runtime=require('../runtime.cjs');
const pending=new Map();
const configuring=new Map();
let inventoryCache=null,inventoryPending=null,inventoryFailure=null,inventoryRevision=0;
function folder(out){return path.join(runtime.logs(out),'pc-link');}
async function prepare(out,pcIp,consoleIp='10.42.0.20',consoleWifiAddress,selectedHosts){
 if(!net.isIPv4(pcIp)||!net.isIPv4(consoleIp))throw Error('Invalid PC link addresses');
 const directory=folder(out);
 await fs.mkdir(directory,{recursive:true,mode:0o700});
 const network=require('../../../core/platform/network.cjs');
 const candidates=await Promise.all(Object.entries(os.networkInterfaces()).map(async ([name,addresses])=>
  await network.isTunnel(name)?[]:addresses.filter(a=>a.family==='IPv4'&&!a.internal).map(a=>a.address)));
 const available=candidates.flat();
 const additional=selectedHosts==null?available:selectedHosts.filter(ip=>available.includes(ip));
 const pcAddresses=[...new Set([pcIp,...additional])].slice(0,8);
 const consoleAddresses=[...new Set([consoleIp,consoleWifiAddress].filter(net.isIPv4))];
 const state=path.join(directory,'connection.json');
 const temporary=state+'.'+randomUUID()+'.tmp';
 try{
  await fs.writeFile(temporary,JSON.stringify({pcIp,consoleIp,pcAddresses,consoleAddresses})+'\n',{mode:0o600,flag:'wx'});
  await fs.rename(temporary,state);
 }finally{await fs.rm(temporary,{force:true});}
 const config=path.join(directory,'pc-link.cfg');
 await fs.writeFile(config,[pcIp,'39524',...pcAddresses.slice(1),''].join('\n'),{mode:0o600});
 return {directory,config};
}
async function status(){
 try {const r=await fetch('http://127.0.0.1:39525/status',{signal:AbortSignal.timeout(700)});return r.ok?await r.json():null;}
 catch{return null;}
}
const ownerRoot=path.resolve(__dirname,'../../..');
function isOwner(value){return value?.service?.root===ownerRoot;}
function verifyOwner(value){
 if(value&&!isOwner(value))
  throw Error('PC Link is running in another IZ2S installation. Stop that service, then refresh the status here.');
 return value;
}
function canRelease(service){
 return service?.enabled===false&&!service.online&&!service.queued&&!service.fileTransfers&&
  !['transferring','staged','applying','verifying'].includes(service.menu?.state)&&
  !Object.values(service.media||{}).some(source=>['starting','running','retrying'].includes(source?.phase));
}
async function releaseForeign(service){
 const {pid,root,name}=service.service||{};
 if(process.platform!=='linux'||name!=='iz2s-pc-link'||!Number.isSafeInteger(pid)||pid<=1||
    typeof root!=='string'||!path.isAbsolute(root))return verifyOwner(service);
 if(!canRelease(service))throw Error('Pause PC Link in the other IZ2S panel, then try again here.');
 const {processIdentity,processAlive}=require('../../../core/session/panel-handover.cjs');
 const proc='/proc/'+pid;
 const [identity,args,info,manifest]=await Promise.all([processIdentity(pid),fs.readFile(proc+'/cmdline','utf8'),
  fs.stat(proc),fs.readFile(path.join(root,'package.json'),'utf8').then(JSON.parse)]).catch(()=>[]);
 if(!identity||info.uid!==process.getuid()||manifest.name!=='iz2s-platform'||
    args.split('\0')[1]!==path.join(root,'engines/iz2s-core/tools/pc-link.cjs'))return verifyOwner(service);
 const latest=await status();
 if(!latest)return;
 if(latest.service?.pid!==pid||latest.service?.root!==root||!canRelease(latest)||
    (await processIdentity(pid))?.start!==identity.start)return verifyOwner(latest);
 try{process.kill(pid,'SIGTERM');}catch(error){if(error.code!=='ESRCH')throw error;}
 for(let elapsed=0;elapsed<3000;elapsed+=100){
  if(!await processAlive(pid))return;
  await new Promise(resolve=>setTimeout(resolve,100));
 }
 throw Error('The other PC Link service is still stopping. Try again.');
}
async function configure(out,consoleIp,{routeTo=require('../../../core/platform/network.cjs').routeTo}={}){
 const directory=folder(out);
 if(configuring.has(directory))throw Error('PC Link setup is already running.');
 configuring.set(directory,true);
 try{
  const {privateIP,sameSubnet}=require('../../../core/session/network-discovery.cjs');
  if(!privateIP(consoleIp))throw Error('Enter the local IPv4 address shown on the console.');
  const route=await routeTo(consoleIp);
  if(!privateIP(route.address)||route.address===consoleIp||!sameSubnet(route.address,consoleIp,route.netmask))
   throw Error('Connect the PC and console to the same local network.');
  const existing=await status();
  if(existing&&!isOwner(existing))await releaseForeign(existing);
  if(isOwner(existing)&&(existing.online||existing.queued||existing.fileTransfers))
   throw Error('Pause PC Link before changing its settings.');
  const {config}=await prepare(out,route.address,consoleIp,undefined,[]);
  const active=await ensure(out);
  if(active?.enabled===false)await setEnabled(out,true);
  return {pcAddress:route.address,config:await fs.readFile(config,'utf8')};
 }finally{configuring.delete(directory);}
}
async function ensure(out){
 const directory=folder(out);
 if(pending.has(directory))return pending.get(directory);
 const task=(async()=>{
  const existing=await status();if(isOwner(existing))return existing;
  try {await fs.access(path.join(directory,'connection.json'));}
  catch(error){if(error.code==='ENOENT')return null;throw error;}
  if(existing)return verifyOwner(existing);
  const log=await fs.open(path.join(directory,'service.log'),'a',0o600);
  let failed=null;
  const child=spawn(process.execPath,[path.join(__dirname,'pc-link.cjs')],{
   windowsHide:true,cwd:__dirname,env:{...process.env,IZ2S_CORE_OUT:runtime.output(out)},detached:true,stdio:['ignore',log.fd,log.fd],
  });
  child.once('error',error=>{failed=error;});child.unref();await log.close();
  for(let i=0;i<12;i++){
   if(failed)throw failed;
   await new Promise(r=>setTimeout(r,100));const value=await status();if(value)return verifyOwner(value);
  }
  throw Error('The Add App service did not start. See /data/log/pc-link/service.log for details.');
 })();
 pending.set(directory,task);
 try{return await task;}finally{pending.delete(directory);}
}
async function snapshot(out){
 const value=await ensure(out);if(!value)return null;
 if(!value.online||!value.files)return {...value,apps:[]};
 const owner=JSON.stringify([value.service?.pid,value.resetEpoch]);
 const receipt=JSON.stringify(value.imported);
 const cached=inventoryCache?.owner===owner?inventoryCache:null;
 if(cached&&cached.receipt===receipt&&Date.now()-cached.at<15000)return {...value,apps:cached.apps};
 const revision=inventoryRevision;
 if(!inventoryPending||inventoryPending.owner!==owner){
  if(inventoryFailure?.owner===owner&&Date.now()-inventoryFailure.at<5000)
   return {...value,apps:cached?.apps||[],inventoryError:inventoryFailure.message};
  const job={owner};
  job.result=(async()=>{
   const r=await fetch(origin+'/installed',{signal:AbortSignal.timeout(6000)});
   const data=await r.json();
   if(!r.ok||!Array.isArray(data.apps))throw Error(data.error||'SD inventory unavailable');
   if(inventoryPending===job&&revision===inventoryRevision){
    inventoryCache={owner,receipt,at:Date.now(),apps:data.apps};
    inventoryFailure=null;
   }
   return data.apps;
  })().catch(error=>{
   if(inventoryPending===job&&revision===inventoryRevision)
    inventoryFailure={owner,at:Date.now(),message:error.message};
   throw error;
  }).finally(()=>{if(inventoryPending===job)inventoryPending=null;});
  inventoryPending=job;
 }
 try{
  const apps=await inventoryPending.result;
  return {...value,apps:revision===inventoryRevision?apps:[]};
 }catch(error){
  return {...value,apps:revision===inventoryRevision?(cached?.apps||[]):[],inventoryError:error.message};
 }
}
const origin='http://127.0.0.1:39525';
async function setEnabled(out,enabled){
 const service=await ensure(out);
 if(!service)throw Error('Configure PC Link on this PC first.');
 if(typeof service.enabled!=='boolean')throw Error('Restart the PC Link service to use connection controls.');
 const response=await fetch(origin+'/connection',{
  method:'POST',headers:{origin,'content-type':'application/json'},
  body:JSON.stringify({enabled}),signal:AbortSignal.timeout(10000),
 });
 const result=await response.json();
 if(!response.ok)throw Error(result.error||'Could not change PC Link.');
 invalidateInventory();
 return result;
}
async function consoleAddresses(out){
 const service=await status();
 const file=path.join(folder(out),'connection.json');
 const config=await fs.readFile(file,'utf8').then(JSON.parse).catch(()=>null);
 const known=Array.isArray(config?.consoleAddresses)?config.consoleAddresses:[config?.consoleIp];
 return [isOwner(service)?service.consoleAddress:undefined,...known];
}
async function install(out,req){
 const service=await ensure(out);
 if(!service?.online||!service.files)throw Error('Open IZ2S on Switch 2 before adding an app.');
 const size=Number(req.headers['content-length']);
 if(!Number.isSafeInteger(size)||size<1||size>256*1024*1024)throw Error('Choose a package up to 256 MiB.');
 if(service.queued)throw Error('The previous package is still queued.');
 const r=await fetch(origin+'/upload',{method:'POST',duplex:'half',body:req,headers:{
  origin,'content-type':'application/octet-stream','content-length':String(size),
 },signal:AbortSignal.timeout(180000)});
 const result=await r.json();
 if(!r.ok)throw Error(result.error||'Could not queue the application.');
 return result;
}
let nroInstallActive=false;
async function installNro(out,req,fileName){
 const nro=require('./nro-package.cjs');
 const id=nro.idFor(fileName);
 const size=Number(req.headers['content-length']);
 if(!Number.isSafeInteger(size)||size<128||size>nro.MAX_NRO)throw Error('Choose an NRO up to 128 MiB.');
 if(nroInstallActive)throw Error('Another NRO installation is in progress.');
 nroInstallActive=true;
 let scratch;
 try{
  const link=verifyOwner(await status());
  if(!link?.online)throw Error('Open IZ2S before installing an NRO.');
  const output=runtime.output(out);
  const temporary=path.join(output,'tmp');
  await fs.mkdir(temporary,{recursive:true});
  scratch=await fs.mkdtemp(path.join(temporary,'nro-'));
  const source=path.join(scratch,'upload.nro'),file=await fs.open(source,'wx+',0o600);
  try{
   let bytes=0;
   for await(const chunk of req){
    bytes+=chunk.length;
    if(bytes>size)throw Error('NRO upload exceeded its declared size.');
    await file.writeFile(chunk);
   }
   if(bytes!==size)throw Error('Incomplete NRO upload.');
   await nro.metadata(size,async(at,length)=>{
    const buffer=Buffer.alloc(length);
    const {bytesRead}=await file.read(buffer,0,length,at);
    if(bytesRead!==length)throw Error('Incomplete NRO metadata.');
    return buffer;
   });
  }finally{await file.close();}
  const python=require('../../../core/platform/python.cjs').command();
  let report;
  try{
   const {stdout}=await promisify(execFile)(python,['-B',path.join(__dirname,'install-nro.py'),id,source],{
    env:{...process.env,IZ2S_CORE_OUT:output},timeout:120000,maxBuffer:65536,
   });
   report=JSON.parse(stdout);
  }catch(error){
   if(/(?:^|\n)ftplib\.error_perm: 530(?:[ -]|$)/.test(error.stderr||'')){
    throw Error(require('./paired-sd.cjs').PROFILE_REJECTED);
   }
   if(/ConnectionRefusedError|ConnectionResetError|timed out/i.test(error.stderr||'')){
    throw Error('Enable FTP on IZ2S with L + R3 and retry the NRO installation.');
   }
   throw Error('Could not transfer the NRO over FTP. Details were saved in the installation log.');
  }
  invalidateInventory();
  return {id,format:'.nro',reused:report.state==='already-installed',displayPath:'SD:/iz2s/modules/'+id+'/launcher.nro'};
 }finally{
  nroInstallActive=false;
  if(scratch)await fs.rm(scratch,{recursive:true,force:true});
 }
}
function invalidateInventory(){
 inventoryRevision++;
 inventoryCache=inventoryPending=inventoryFailure=null;
}
async function reset(out){
 // Reset must also work after console reboot or before PC Link was configured.
 // Do not start a service just to discard an old session.
 invalidateInventory();
 const service=await status();
 if(!isOwner(service))return {ok:true,offline:true};
 const response=await fetch(origin+'/reset',{method:'POST',headers:{origin,'content-type':'application/json'},body:'{}',signal:AbortSignal.timeout(125000)});
 const result=await response.json();
 if(!response.ok)throw Error(result.error||'Could not reset PC Link.');
 invalidateInventory();
 return result;
}
async function menuUpdate(out,{retry=false}={}){
 const service=await ensure(out);
 if(!service?.menu?.supported)throw Error('Open IZ2S with menu updates enabled.');
 const response=await fetch(origin+'/menu-update',{method:'POST',headers:{origin,'content-type':'application/json'},
  body:JSON.stringify({retry}),signal:AbortSignal.timeout(10000)});
 const result=await response.json();
 if(!response.ok)throw Error(result.error||'Could not start the IZ2S menu update.');
 return result;
}
async function removeNro(out, id, service) {
 const inventory = await fetch(origin + '/installed', {signal: AbortSignal.timeout(6000)});
 const body = await inventory.json();
 if (!inventory.ok || !Array.isArray(body.apps)) throw Error(body.error || 'SD inventory unavailable');
 const app = body.apps.find(row => row.id === id && row.format === '.nro');
 const target = require('./nro-package.cjs').installedPath(app);
 if (!Number.isSafeInteger(app.bytes) || app.bytes <= 0) throw Error('The installed NRO size is unavailable.');
 const ftp = require('./paired-sd.cjs').create(folder(out), [service.consoleAddress]);
 if (!await ftp.online()) throw Error(ftp.problem() || 'Enable FTP on IZ2S with L + R3 to remove an NRO.');
 const current = verifyOwner(await status());
 if (!current?.online || current.consoleAddress !== service.consoleAddress ||
     current.service.pid !== service.service.pid || current.resetEpoch !== service.resetEpoch ||
     current.fileTransfers) throw Error('The console session changed or is transferring files. Retry when it is idle.');
 await ftp.call('remove-nro', target.slice(3), String(app.bytes));
 return {removed: id};
}
async function remove(out,id,format='.iza'){
 if(typeof id!=='string'||!/^[a-z0-9][a-z0-9-]{0,46}$/.test(id))throw Error('Select a valid installed app.');
 const service=await ensure(out);
 if(!service?.online||!service.files)throw Error('Open IZ2S on Switch 2 before removing an app.');
 if (format === '.nro') {
  if (!/^nro-[a-f0-9]{16}$/.test(id)) throw Error('Select an installed NRO.');
  const result = await removeNro(out, id, service);
  invalidateInventory();
  return result;
 }
 if (format !== '.iza') throw Error('Unrecognized application format.');
 const r=await fetch(origin+'/installed/remove',{method:'POST',headers:{origin,'content-type':'text/plain'},body:id,signal:AbortSignal.timeout(35000)});
 const result=await r.json();
 if(!r.ok)throw Error(result.error||'Could not remove the application.');
 invalidateInventory();
 return result;
}
module.exports={setEnabled,consoleAddresses,prepare,configure,ensure,folder,status,snapshot,install,installNro,invalidateInventory,remove,reset,menuUpdate};
