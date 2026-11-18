// Select only an audio stream whose process belongs to this owned application.
const fs=require('node:fs/promises'),crypto=require('node:crypto'),{execFile}=require('node:child_process'),{promisify}=require('node:util'),run=promisify(execFile);
async function silentSink({execute=run}={}){
 const name='iz_youtube_'+crypto.randomBytes(6).toString('hex'),{stdout}=await execute('pactl',['load-module','module-null-sink','sink_name='+name,'sink_properties=device.description=IZ_YouTube_Stream','rate=48000','channels=2'],{timeout:5000,maxBuffer:16384}),module=Number(stdout.trim());
 if(!Number.isInteger(module)||module<0)throw Error('Failed to create silent YouTube audio output');let closed=false;
 return {name,module,env:{PULSE_SINK:name},async close(){if(closed)return;closed=true;await execute('pactl',['unload-module',String(module)],{timeout:5000,maxBuffer:16384});}};
}
async function descendants(root){
 const entries=await fs.readdir('/proc'),parents=new Map();
 await Promise.all(entries.filter(n=>/^\d+$/.test(n)).map(async n=>{try{const s=await fs.readFile('/proc/'+n+'/stat','utf8'),tail=s.slice(s.lastIndexOf(')')+2).split(' ');parents.set(Number(n),Number(tail[1]));}catch{}}));
 const owned=new Set([root]);for(let changed=true;changed;){changed=false;for(const [pid,parent]of parents)if(owned.has(parent)&&!owned.has(pid)){owned.add(pid);changed=true;}}return owned;
}
function select(inputs,sinks,pids,graph=[]){
 const clients=new Set(graph.filter(n=>pids.has(Number(n.info?.props?.['application.process.id']))).map(n=>n.id));
 const serials=new Set(graph.filter(n=>n.type==='PipeWire:Interface:Node'&&clients.has(Number(n.info?.props?.['client.id']))&&n.info?.props?.['media.class']==='Stream/Output/Audio').map(n=>Number(n.info.props['object.serial'])));
 const input=inputs.find(i=>pids.has(Number(i.properties?.['application.process.id']))||serials.has(i.index)||serials.has(Number(i.properties?.['object.serial'])));if(!input)return null;
 const sink=sinks.find(s=>s.index===input.sink);if(!sink?.monitor_source?.endsWith('.monitor'))throw Error('YouTube audio monitor unavailable');return {input:input.index,device:sink.monitor_source};
}
async function target(pid){
 const parse=async args=>JSON.parse((await run(args[0],args.slice(1),{timeout:4000,maxBuffer:8*1048576})).stdout);
 const [inputs,sinks,pids]=await Promise.all([parse(['pactl','--format=json','list','sink-inputs']),parse(['pactl','--format=json','list','sinks']),descendants(pid)]);
 let found=select(inputs,sinks,pids);if(!found)found=select(inputs,sinks,pids,await parse(['pw-dump']));return found;
}
function args(t){if(!Number.isInteger(t.input)||t.input<0||!t.device?.endsWith('.monitor'))throw Error('Invalid YouTube audio source');return ['--raw','--format=s16le','--rate=48000','--channels=2','--latency-msec=20','--client-name=IZ YouTube stream','--stream-name=YouTube to Switch','--device='+t.device,'--monitor-stream='+t.input];}
function sinkArgs(name){if(!/^iz_youtube_[a-f0-9]{12}$/.test(name))throw Error('Invalid owned YouTube sink');return ['--raw','--format=s16le','--rate=48000','--channels=2','--latency-msec=20','--client-name=IZ YouTube stream','--stream-name=YouTube to Switch','--device='+name+'.monitor'];}
module.exports={descendants,select,target,args,silentSink,sinkArgs};
