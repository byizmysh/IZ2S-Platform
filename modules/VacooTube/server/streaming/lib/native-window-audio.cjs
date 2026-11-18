// Capture a verified application's output stream, never a whole sink or microphone.
const fs=require('node:fs/promises'),path=require('node:path'),{execFile}=require('node:child_process'),runDefault=require('node:util').promisify(execFile);
const binary=value=>path.basename(String(value).replaceAll('\\','/')).replace(/ \(deleted\)$/,'').toLowerCase();
async function processIdentity(pid,{procRoot='/proc',readFile=fs.readFile,readlink=fs.readlink,maxProcesses=128}={}){
 if(!Number.isInteger(pid)||pid<=1)throw Error('Invalid selected audio process');
 const processes=new Map(),queue=[pid],visited=new Set();
 while(queue.length&&visited.size<maxProcesses){
  const own=queue.shift();if(visited.has(own))continue;visited.add(own);const folder=path.join(procRoot,String(own));
  try{
   const [status,cmd,exe]=await Promise.all([readFile(path.join(folder,'status'),'utf8'),readFile(path.join(folder,'cmdline'),'utf8'),readlink(path.join(folder,'exe')).catch(()=> '')]);
   const names=new Set([binary(String(cmd).split('\0')[0]),binary(exe)].filter(Boolean));if(!names.size)throw Error('Selected audio process has no executable identity');
   const namespace=(String(status).match(/^NSpid:\s+(.+)$/m)?.[1]||'').trim().split(/\s+/).map(Number).filter(n=>Number.isSafeInteger(n)&&n>0);processes.set(own,{ids:new Set([own,...namespace]),names});
   const children=String(await readFile(path.join(folder,'task',String(own),'children'),'utf8').catch(()=> ''));
   for(const child of children.trim().split(/\s+/).map(Number))if(Number.isSafeInteger(child)&&child>1&&!visited.has(child))queue.push(child);
  }catch(error){if(own===pid)throw Error('Selected application process is unavailable');}
 }
 if(queue.length)throw Error('Selected application process tree is too large to identify safely');
 return {pid,processes};
}
function audioOwned(props,identity){
 const secure=Number(props?.['pipewire.sec.pid']);
 // A present authoritative host PID cannot be overridden by a claimed namespace PID.
 if(props&&Object.hasOwn(props,'pipewire.sec.pid'))return Number.isSafeInteger(secure)&&secure>0&&identity.processes.has(secure);
 const id=Number(props?.['application.process.id']),name=binary(props?.['application.process.binary']);
 return !!name&&[...identity.processes.values()].some(process=>process.ids.has(id)&&process.names.has(name));
}
async function audioTarget(pid,{run=runDefault,...options}={}){
 const parse=async(name,args,maxBuffer=1048576)=>{const result=await run(name,args,{timeout:4000,maxBuffer});return JSON.parse(result.stdout);};
 const [identity,inputs,sinks]=await Promise.all([processIdentity(pid,options),parse('pactl',['--format=json','list','sink-inputs']),parse('pactl',['--format=json','list','sinks'])]);
 if(!Array.isArray(inputs)||!Array.isArray(sinks))throw Error('Invalid application audio inventory');if(!inputs.length)return null;
 let matches=inputs.filter(input=>audioOwned(input.properties,identity));
 if(!matches.length){
  const graph=await parse('pw-dump',[],8*1048576);if(!Array.isArray(graph))throw Error('Invalid PipeWire application inventory');
  const clients=new Set(graph.filter(item=>audioOwned(item.info?.props,identity)).map(item=>item.id));
  const serials=new Set(graph.filter(item=>item.type==='PipeWire:Interface:Node'&&item.info?.props?.['media.class']==='Stream/Output/Audio'&&(audioOwned(item.info.props,identity)||clients.has(Number(item.info.props['client.id'])))).map(item=>Number(item.info.props['object.serial'])).filter(Number.isSafeInteger));
  matches=inputs.filter(input=>{const props=input.properties,serial=Number(props?.['object.serial']);if(props&&Object.hasOwn(props,'pipewire.sec.pid')&&!audioOwned(props,identity))return false;return Number.isSafeInteger(serial)&&serials.has(serial);});
 }
 const active=matches.filter(input=>input.corked!==true&&input.corked!=='yes');if(active.length)matches=active;
 if(!matches.length)return null;
 if(matches.length!==1)throw Error('Selected application has multiple audio streams; choose a more specific source');
 const input=matches[0],sink=sinks.find(s=>s.index===input.sink);if(!sink)return null;if(!Number.isSafeInteger(input.index)||input.index<0||input.index>0xffffffff||!sink.monitor_source?.endsWith('.monitor'))throw Error('Selected application audio monitor is unavailable');
 return {input:input.index,device:sink.monitor_source,pid,scope:'selected application audio only'};
}
function audioArgs(target){
 if(!target||!Number.isSafeInteger(target.input)||target.input<0||target.input>0xffffffff||typeof target.device!=='string'||target.device.length>1024||/[\x00-\x1f]/.test(target.device)||!target.device.endsWith('.monitor'))throw Error('Invalid selected application audio source');
 return ['--raw','--format=s16le','--rate=48000','--channels=2','--latency-msec=20','--process-time-msec=10','--client-name=IZ window stream','--stream-name=Selected application to Switch','--device='+target.device,'--monitor-stream='+target.input];
}
module.exports={processIdentity,audioOwned,audioTarget,audioArgs};
