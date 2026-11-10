'use strict';
// A real local TCP connection, the production protocol/C server and a 1 ms
// worker yield. Reports throughput; it makes no console performance claim.
const fs=require('node:fs/promises'),path=require('node:path'),net=require('node:net'),{spawn,execFileSync}=require('node:child_process'),{once}=require('node:events'),assert=require('node:assert/strict');
const root=path.resolve(__dirname,'../..'),out=path.join(root,'data/build-cache/recovery-performance'),W=require('../../core/console/recovery/wire.cjs');
async function measure(source,label,{count=64,journal=false}={}){
 const binary=path.join(out,'throughput-'+label);execFileSync('cc',['-std=c11','-O2','-Icore/console/recovery','tests/sd-launcher/recovery-throughput.c',source,'core/console/recovery/wire.c','core/console/recovery/crypto.c','core/console/recovery/journal.c','-o',binary],{cwd:root});
 const child=spawn(binary,[],{cwd:root,stdio:['ignore','pipe','pipe']}),done=once(child,'close');let output='',errors='',socket,connection;
 child.stderr.on('data',b=>errors+=b);
 try{
  const port=await new Promise((resolve,reject)=>{const timer=setTimeout(()=>reject(Error('Benchmark listener timeout')),5000);child.stdout.on('data',b=>{output+=b;const m=output.match(/READY (\d+)/);if(m){clearTimeout(timer);resolve(Number(m[1]));}});child.once('error',reject);child.once('close',()=>{clearTimeout(timer);reject(Error(errors));});});
  const dial=async()=>{const s=net.createConnection({host:'127.0.0.1',port});s.on('error',()=>{});await once(s,'connect');return s;},credentials={session:'17'.repeat(16),key:'42'.repeat(32)},transport=require('../../core/console/recovery/connection.cjs');
  let request;
  if(journal){const directory=await fs.mkdtemp(path.join(out,'journal-'));connection=await transport.open({directory,credentials,candidates:async()=>[{address:'10.42.0.20',port}],connect:dial,routeTimeoutMs:2000,pingMs:60000});await connection.start();request=async(sequence,payload)=>{await connection.write(payload);return connection.read(16+0x8000);};}
  else{socket=await dial();const peer=await transport.peer(socket,credentials,2000);request=async(sequence,payload)=>{await peer.send(W.TYPES.REQUEST,sequence,payload);const frame=await peer.read();assert.equal(frame.type,W.TYPES.RESPONSE);return frame.payload;};}
  const times=[],payload=Buffer.alloc(0x8080,1),start=performance.now();
  for(let sequence=1;sequence<=count;sequence++){const at=performance.now(),response=await request(sequence,payload);assert.equal(response.readBigUInt64LE(8),BigInt(sequence));times.push(performance.now()-at);}
  const elapsedMs=performance.now()-start,sorted=[...times].sort((a,b)=>a-b);connection?.close();socket?.destroy();child.kill('SIGTERM');assert.equal((await done)[0],0,errors);
  return {label,journal,count,elapsedMs,meanMs:elapsedMs/count,medianMs:sorted[Math.floor(count/2)],p95Ms:sorted[Math.floor(count*.95)],outputMiBPerSecond:count*0x8000/(1024*1024)/(elapsedMs/1000),transport:output.match(/STOPPED .*/)?.[0],errors};
 }finally{connection?.close();socket?.destroy();child.kill('SIGTERM');await done;}
}
async function main(){await fs.mkdir(out,{recursive:true});const results=[];for(const journal of [false,true])for(const [label,source]of [['before','data/build-cache/recovery-performance/server-before.c'],['after','core/console/recovery/server.c']]){const r=await measure(source,label+'-'+(journal?'journal':'wire'),{journal});results.push(r);console.log(JSON.stringify(r));}const report={at:new Date().toISOString(),localOnly:true,consoleQualified:false,results};await fs.writeFile(path.join(root,'data/diagnostics/recovery-throughput-local.json'),JSON.stringify(report,null,2)+'\n');}
if(require.main===module)main().catch(e=>{console.error(e);process.exitCode=1;});module.exports={measure,main};
