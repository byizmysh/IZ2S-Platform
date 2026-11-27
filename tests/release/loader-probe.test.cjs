'use strict';
const test=require('node:test'),assert=require('node:assert/strict');
const fs=require('node:fs/promises'),path=require('node:path'),os=require('node:os'),net=require('node:net');
const {create}=require('../../engines/iz2sciv-network/session.cjs');
const discovery=require('../../engines/iz2sciv-shared/runtime/discovery.cjs');
const host='192.168.99.20';

async function fixture(t,respond,{dependenciesReady=true}={}){
 const root=await fs.mkdtemp(path.join(os.tmpdir(),'iz2s-loader-probe-'));
 const sockets=new Set(),requests=[];
 let received;
 const firstRequest=new Promise(resolve=>{received=resolve;});
 const server=net.createServer({allowHalfOpen:true},socket=>{
  sockets.add(socket);socket.on('error',()=>{});socket.on('close',()=>sockets.delete(socket));
  let code='';socket.on('data',chunk=>{code+=chunk;});
  socket.on('end',()=>{requests.push(code);received(code);respond(socket,code);});
 });
 await new Promise((resolve,reject)=>{server.once('error',reject);server.listen(0,'127.0.0.1',resolve);});
 const session=create({root,settings:{},connections:{snapshot:()=>({})},platform:'linux',
  inspect:async()=>({supported:true,dependenciesReady,keysReady:true,capabilitiesReady:true,networkManager:true,radios:['phy0']}),
  launch:()=>{throw Error('A protocol-only check must not start Python or another process');},
  protocolProbe:(address,options)=>discovery.probe(address,{...options,connect:target=>{
   assert.deepEqual(target,{host,port:9026});
   return net.createConnection({host:'127.0.0.1',port:server.address().port});
  }}),
 });
 t.after(async()=>{
  await session.stop();for(const socket of sockets)socket.destroy();
  await new Promise(resolve=>server.close(resolve));
  await fs.rm(root,{recursive:true,force:true});
 });
 async function check(){
  await session.action({action:'check-loader',host});
  await session.waitForOperation();
  return (await session.snapshot()).civswitchLdn;
 }
 return {root,session,check,requests,firstRequest};
}

function confirm(socket,code){
 const marker=/IZ2S_CIV_[a-f0-9]{24}/.exec(code)?.[0];
 assert(marker,'The probe must include an unpredictable marker');
 assert.match(code,/^\(async\(\)=>\{await log\('IZ2S_CIV_[a-f0-9]{24}'\)\}\)\(\)$/);
 socket.write(marker+'\n');
 setImmediate(()=>socket.end('Executed successfully\n'));
}

for(const dependenciesReady of [true,false])test('Linux loader verification works without Python probe files (LDN dependencies '+dependenciesReady+')',{timeout:4000},async t=>{
 const f=await fixture(t,confirm,{dependenciesReady});
 const result=await f.check();
 assert.equal(result.loaderReady,true);assert.equal(result.phase,'ready');assert.equal(result.error,undefined);
 assert.equal(f.requests.length,1);
 await assert.rejects(fs.stat(path.join(f.root,'engines/iz2sciv-shared/probe.py')),{code:'ENOENT'});
});

test('An open port with the wrong marker invalidates a previous ready result',{timeout:4000},async t=>{
 let valid=true;
 const f=await fixture(t,(socket,code)=>valid?confirm(socket,code):socket.end('Executed successfully\nIZ2S_CIV_wrong\n'));
 assert.equal((await f.check()).loaderReady,true);
 valid=false;
 const result=await f.check();
 assert.equal(result.loaderReady,false);assert.equal(result.phase,'entered');
 assert.match(result.error,/not responding on TCP 9026/);
 assert.equal(f.requests.length,2);
});

test('Stopping a silent protocol probe aborts the socket and never confirms a loader',{timeout:4000},async t=>{
 const f=await fixture(t,()=>{});
 await f.session.action({action:'check-loader',host});
 await f.firstRequest;
 await f.session.stop();
 const result=(await f.session.snapshot()).civswitchLdn;
 assert.equal(result.loaderReady,false);assert.equal(result.busy,false);
 assert.equal(f.requests.length,1);
});
