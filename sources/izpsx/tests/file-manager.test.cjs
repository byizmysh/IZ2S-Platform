'use strict';
const test=require('node:test'),assert=require('node:assert/strict'),{once}=require('node:events');
const {create,target,AREAS}=require('../server/settings.cjs');
test('PS1 manager: scoped folders, uploads, deletion, busy/session guards, auth and independent view shutdown',async()=>{
 const calls=[],files=new Map([[AREAS.games,[{name:'Tekken 3',type:'dir',size:0}]],[AREAS.games+'/Tekken 3',[{name:'Tekken 3.cue',type:'file',size:72}]],[AREAS.bios,[]],[AREAS.saves,[{name:'card1.a.izmc',type:'file',size:131136}]]]);
 let current={ready:true,session:'launcher-model'},storage={status:'ready',mode:'storage',lease:'shared',session:current.session,uploads:0,downloads:0},pending;
 const backend={async request(method,data={}){
  calls.push({method,...data});if(method==='status')return {...storage};
  if(method==='start-storage'){storage={...storage,status:'ready',mode:'storage',lease:'own'};return {...storage};}
  if(method==='browse')return {cwd:data.path,entries:files.get(data.path)||[],writable:true};
  if(method==='sd-mkdir'){if(!files.has(data.path))files.set(data.path,[]);return {ok:true};}
  if(method==='sd-upload-begin'){pending={...data,offset:0};return {token:'upload-token',bytes:0,chunkBytes:0x70000};}
  if(method==='sd-upload-chunk'){assert.equal(data.offset,pending.offset);pending.offset+=Buffer.from(data.data,'base64').length;return {bytes:pending.offset};}
  if(method==='sd-upload-finish'){assert.equal(pending.offset,pending.size);const result={ok:true,path:pending.path,bytes:pending.size};pending=null;return result;}
  if(method==='sd-delete')return {ok:true};if(method==='sd-upload-cancel'){pending=null;return {ok:true};}
  if(method==='stop'){assert.equal(data.lease,'own');storage.status='closed';return {...storage};}
  throw Error('Unexpected backend action '+method);
 }};
 backend.requestRaw=async(method,data,timeout)=>{assert.equal(method,'start-storage');assert.equal(timeout,300000);return backend.request(method,data);};
 const services={async listenHttp(server){server.listen(0,'127.0.0.1');await once(server,'listening');return {origin:'http://127.0.0.1:'+server.address().port};}};
 const handle=await create({root:'/unused-model',services,backend,guard:async()=>({...current})});const url=new URL(handle.url),origin=url.origin,key=url.hash.slice(1);
 async function request(route,data,options={}){const r=await fetch(origin+route,{method:data===undefined?'GET':'POST',headers:{Origin:origin,'x-izpsx-key':key,...(data===undefined?{}:{'content-type':'application/json'}),...options},...(data===undefined?{}:{body:JSON.stringify(data)})});return {status:r.status,data:await r.json()};}
 try{
  assert.equal((await request('/api/status',undefined,{'x-izpsx-key':'wrong'})).status,400);
  assert.equal((await request('/api/connect',{area:'games'}, {Origin:'http://example.invalid'})).status,400);
  let r=await request('/api/connect',{area:'games'});assert.equal(r.status,200);assert.equal(r.data.entries[0].type,'dir');
  r=await request('/api/files',{area:'games',relative:'Tekken 3'});assert.equal(r.data.entries[0].name,'Tekken 3.cue');
  for(const bad of ['../outside.bin','/outside.bin','dir//x.bin','dir/../x.bin','bad\\x.bin'])assert.throws(()=>target('games',bad));
  const before=calls.filter(c=>c.method==='sd-delete').length;
  assert.equal((await request('/api/delete',{area:'saves',name:'card1.a.izmc',confirmed:'card1.a.izmc'})).status,400);
  assert.equal((await request('/api/delete',{area:'games',name:'Tekken 3',type:'dir',confirmed:false})).status,400);
  assert.equal(calls.filter(c=>c.method==='sd-delete').length,before);
  r=await request('/api/delete',{area:'games',relative:'',name:'Tekken 3',type:'dir',size:0,confirmed:'Tekken 3'});assert.equal(r.status,200);assert.equal(calls.at(-1).path,AREAS.games+'/Tekken 3');
  assert.equal((await request('/api/upload/begin',{area:'bios',name:'wrong.bin',size:524288})).status,400);
  assert.equal((await request('/api/upload/begin',{area:'saves',name:'card1.a.izmc',size:131136})).status,400);
  r=await request('/api/upload/begin',{area:'games',name:'New Game/disc.bin',size:4});assert.equal(r.status,200);assert.equal(calls.find(c=>c.method==='sd-upload-begin').path,AREAS.games+'/New Game/disc.bin');
  r=await request('/api/upload/chunk',{token:'upload-token',offset:0,data:Buffer.from([1,2,3,4]).toString('base64')});assert.equal(r.data.bytes,4);
  r=await request('/api/upload/finish',{token:'upload-token'});assert.equal(r.data.path,AREAS.games+'/New Game/disc.bin');
  const large=Buffer.alloc(0x70000+17);for(let i=0;i<large.length;i++)large[i]=i%251;
  r=await request('/api/upload/begin',{area:'games',name:'large.bin',size:large.length});assert.equal(r.data.chunkBytes,0x70000);
  for(let at=0;at<large.length;at+=0x70000){const chunk=large.subarray(at,at+0x70000);r=await request('/api/upload/chunk',{token:'upload-token',offset:at,data:chunk.toString('base64')});assert.equal(r.status,200);assert.equal(r.data.bytes,at+chunk.length);}
  r=await request('/api/upload/finish',{token:'upload-token'});assert.equal(r.data.bytes,large.length);
  r=await request('/api/disconnect',{});assert.equal(r.data.shared,true);assert(!calls.some(c=>c.method==='stop'));
  current.ready=false;const n=calls.length;r=await request('/api/files',{area:'games'});assert.equal(r.status,400);assert.equal(calls.length,n);current.ready=true;
  storage.status='closed';await request('/api/connect',{area:'games'});assert(calls.some(c=>c.method==='start-storage'));
  r=await request('/api/upload/begin',{area:'games',name:'another.bin',size:1});assert.equal(r.status,200);
  current.session='launcher-new';r=await request('/api/upload/chunk',{token:'upload-token',offset:0,data:'AA=='});assert.equal(r.status,400);current.session='launcher-model';
  await request('/api/upload/cancel',{token:'upload-token'});
 }finally{await handle.close();}
 assert.equal(calls.filter(c=>c.method==='stop').length,0,'Closing the view must leave the shared file service available');
});

test('PS1 joins preparing SD storage and switches an idle game export without closing the shared browser',async()=>{
 for(const mode of ['storage','game','busy','changed','stopping']){
  let calls=[],polls=0,storage={status:mode==='storage'?'preparing':mode==='stopping'?'stopping':'ready',mode:mode==='storage'?'storage':'game',lease:'shared',session:'launcher-model',uploads:mode==='busy'?1:0,downloads:0};
  const backend={async request(method,data={}){calls.push(method);if(method==='status'){if(mode==='storage'&&++polls>1)storage.status='ready';if(mode==='stopping'&&storage.status==='stopping'&&++polls>1)storage.status='closed';return {...storage};}if(method==='start-storage'){storage={...storage,mode:'storage',status:'ready',lease:'next',session:mode==='changed'?'another-console':'launcher-model'};return {...storage};}if(method==='browse')return {cwd:data.path,entries:[],writable:true};if(method==='stop'){storage.status='closed';return {...storage};}throw Error('Unexpected mutation '+method);}};
  const services={async listenHttp(server){server.listen(0,'127.0.0.1');await once(server,'listening');return {origin:'http://127.0.0.1:'+server.address().port};}};
  const handle=await create({root:'/unused',services,backend,guard:async()=>({ready:true,session:'launcher-model'})});const url=new URL(handle.url);
  try{const result=await fetch(url.origin+'/api/connect',{method:'POST',headers:{Origin:url.origin,'x-izpsx-key':url.hash.slice(1),'content-type':'application/json'},body:JSON.stringify({area:'games'})});
   assert.equal(result.status,['busy','changed'].includes(mode)?400:200);
   if(mode==='busy')assert(!calls.includes('start-storage'));
   if(mode==='storage')assert(!calls.includes('start-storage'));
   if(mode==='game')assert.equal(calls.filter(c=>c==='start-storage').length,1);
   if(mode==='changed')assert(!calls.includes('browse'));
  }finally{await handle.close();}
  if(mode!=='stopping')assert(!calls.includes('stop'),'A borrowed Super File Bros. session must remain open');
 }
});

test('PS1 browser uses negotiated 448 KiB chunks without overflowing JavaScript argument limits',async()=>{
 const fs=require('node:fs'),vm=require('node:vm'),path=require('node:path'),source=fs.readFileSync(path.join(__dirname,'../server/ui/app.js'),'utf8');
 const bytes=Buffer.alloc(0x70000*2+17);for(let i=0;i<bytes.length;i++)bytes[i]=(i*73+19)&255;
 const file={name:'disc.bin',size:bytes.length,slice:(a,b)=>({arrayBuffer:async()=>Uint8Array.from(bytes.subarray(a,b)).buffer})},nodes=new Map(),chunks=[];
 const context={crypto:require("node:crypto").webcrypto,DataView,BigInt,area:'games',relative:'',uploadToken:null,cached:{entries:[]},IzpsxImport:{prepare:async()=>({items:[{file,name:file.name}]})},Uint8Array,btoa:s=>Buffer.from(s,'binary').toString('base64'),size:String,controls(){},status:async()=>{},browse:async()=>{},$:id=>{if(!nodes.has(id))nodes.set(id,{dataset:{},replaceChildren(){}});return nodes.get(id);},api:async(route,data)=>{if(route.endsWith('/begin'))return {token:'large',chunkBytes:0x70000};if(route.endsWith('/chunk')){chunks.push({offset:data.offset,bytes:Buffer.from(data.data,'base64')});return {bytes:data.offset+chunks.at(-1).bytes.length};}return {ok:true};}};
 const upload=vm.runInNewContext(source.slice(source.indexOf('async function upload('),source.indexOf("\n$('connect')"))+'\nupload;',context);await upload([file]);
 assert.deepEqual(chunks.map(c=>c.bytes.length),[0x70000,0x70000,17]);assert(Buffer.concat(chunks.map(c=>c.bytes)).equals(bytes));assert.equal(context.uploadToken,null);
});

test('PS1 cancellation hides progress immediately after acknowledgement, including the final chunk',async()=>{
 const fs=require('node:fs'),vm=require('node:vm'),path=require('node:path'),source=fs.readFileSync(path.join(__dirname,'../server/ui/app.js'),'utf8');
 const nodes=new Map(),calls=[],file={name:'disc.bin',size:4,slice:()=>({arrayBuffer:async()=>Uint8Array.of(1,2,3,4).buffer})};
 const context={crypto:require("node:crypto").webcrypto,DataView,BigInt,area:'games',relative:'',uploadToken:null,cached:{entries:[]},IzpsxImport:{prepare:async()=>({items:[{file,name:file.name}]})},Uint8Array,btoa:s=>Buffer.from(s,'binary').toString('base64'),size:String,controls(){},status:async()=>{},browse:async()=>{},$:id=>{if(!nodes.has(id))nodes.set(id,{dataset:{},replaceChildren(){}});return nodes.get(id);},api:async(route)=>{calls.push(route);if(route.endsWith('/begin'))return {token:'cancelled',chunkBytes:0x70000};if(route.endsWith('/chunk')){context.cancel=true;return {bytes:4};}return {ok:true};}};
 const upload=vm.runInNewContext(source.slice(source.indexOf('async function upload('),source.indexOf("\n$('connect')"))+'\nupload;',context);
 await assert.rejects(upload([file]),/Upload cancelled/);assert.equal(context.uploadToken,null);assert.equal(nodes.get('upload-progress').hidden,true);assert.equal(calls.filter(c=>c.endsWith('/cancel')).length,1);assert(!calls.some(c=>c.endsWith('/finish')));
});

test('PS1 status clears a finished transfer and ignores stale replies which would resurrect it',async()=>{
 const fs=require('node:fs'),vm=require('node:vm'),path=require('node:path'),source=fs.readFileSync(path.join(__dirname,'../server/ui/app.js'),'utf8'),nodes=new Map(),pending=[];
 const context={crypto:require("node:crypto").webcrypto,DataView,BigInt,statusSequence:0,connectionSession:null,uploading:false,uploadToken:'old',controls(){},$:id=>{if(!nodes.has(id))nodes.set(id,{dataset:{},replaceChildren(){}});return nodes.get(id);},api:()=>new Promise(resolve=>pending.push(resolve))};
 const status=vm.runInNewContext(source.slice(source.indexOf('async function status()'),source.indexOf('\nasync function upload('))+'\nstatus;',context);
 const old=status(),fresh=status();pending[1]({ready:true,attached:true,pendingUpload:null});await fresh;assert.equal(nodes.get('upload-progress').hidden,true);assert.equal(context.uploadToken,null);
 pending[0]({ready:true,attached:true,pendingUpload:{token:'stale',name:'old.bin'}});await old;assert.equal(nodes.get('upload-progress').hidden,true);assert.equal(context.uploadToken,null);
});

test('PS1 settings can close and reopen after a lost console with a durable resumable file job',async t=>{
 const fs=require('node:fs/promises'),os=require('node:os'),path=require('node:path'),dataDirectory=await fs.mkdtemp(path.join(os.tmpdir(),'ps1-resume-view-'));t.after(()=>fs.rm(dataDirectory,{recursive:true,force:true}));let consoleState={ready:true,session:'old'},stored={status:'ready',mode:'storage',lease:'lease',session:'old',uploads:0,downloads:0},offset=0;
 const backend={async request(method,data={}){if(method==='status')return {...stored};if(method==='browse')return {cwd:data.path,entries:[],writable:true};if(method==='sd-mkdir')return {ok:true};if(method==='sd-upload-begin'){stored.uploads=1;return {token:'token',bytes:0,chunkBytes:458752};}if(method==='sd-upload-chunk'){offset+=Buffer.from(data.data,'base64').length;return {bytes:offset};}if(method==='start-storage'){stored={...stored,status:'ready',lease:'new-lease',session:'new'};return {...stored};}if(method==='sd-upload-resume')return {token:data.token,bytes:offset,size:6,chunkBytes:458752,chunks:[]};throw Error('Unexpected '+method);}};
 const services={async listenHttp(server){server.listen(0,'127.0.0.1');await once(server,'listening');return {origin:'http://127.0.0.1:'+server.address().port};}};let handle;
 async function req(route,data){const u=new URL(handle.url),r=await fetch(u.origin+route,{method:data?'POST':'GET',headers:{Origin:u.origin,'x-izpsx-key':u.hash.slice(1),'content-type':'application/json'},...(data?{body:JSON.stringify(data)}:{})});const result=await r.json();assert.equal(r.status,200,result.error);return result;}
 handle=await create({root:'/unused',services,backend,guard:async()=>({...consoleState}),dataDirectory});await req('/api/upload/begin',{area:'games',name:'disc.bin',size:6,fingerprint:'same'});await req('/api/upload/chunk',{token:'token',offset:0,data:'AQID'});
 consoleState={ready:false};stored.status='failed';await handle.close();assert.equal(JSON.parse(await fs.readFile(path.join(dataDirectory,'uploads.json')))[0][1].offset,3);
 consoleState={ready:true,session:'new'};handle=await create({root:'/unused',services,backend,guard:async()=>({...consoleState}),dataDirectory});try{const status=await req('/api/status');assert.equal(status.pendingUpload.resumeAllowed,true);const r=await req('/api/upload/resume',{token:'token',name:'disc.bin',size:6,fingerprint:'same'});assert.equal(r.bytes,3);}finally{consoleState={ready:false};await handle.close();}
});

test('Stop closes the PS1 view promptly with a hung connected transfer or an offline status request',async()=>{
 for(const connected of [true,false]){
  let entered,release,backendCalls=0;
  const started=new Promise(resolve=>entered=resolve),blocked=new Promise(resolve=>release=resolve);
  const services={async listenHttp(server){server.listen(0,'127.0.0.1');await once(server,'listening');return {origin:'http://127.0.0.1:'+server.address().port};}};
  const handle=await create({root:'/unused',services,
   guard:async()=>{if(!connected){entered();await blocked;}return {ready:connected,session:'session'};},
   backend:{async request(method,data){backendCalls++;if(method==='status')return {status:'ready',mode:'storage',lease:'shared',session:'session'};if(method==='browse'){entered();await blocked;return {cwd:data.path,entries:[]};}throw Error('Unexpected mutation '+method);}}});
  const u=new URL(handle.url),response=fetch(u.origin+(connected?'/api/connect':'/api/status'),{method:connected?'POST':'GET',headers:{Origin:u.origin,'x-izpsx-key':u.hash.slice(1),'content-type':'application/json'},...(connected?{body:'{"area":"games"}'}:{})}).catch(()=>null);
  try{
   await started;
   let timer;try{await Promise.race([Promise.all([handle.close(),handle.close()]),new Promise((_,reject)=>{timer=setTimeout(()=>reject(Error('Stop waited for the console')),750);})]);}finally{clearTimeout(timer);}
   assert.equal(handle.server.listening,false);
   const calls=backendCalls;await handle.close();assert.equal(backendCalls,calls,'Stop must not send additional console commands');
  }finally{release();await response;await handle.close();}
 }
});

test('PS1 replaces an old failed export with the current console SD session without replaying file mutations',async()=>{
 let starts=0,state={status:'failed',mode:'storage',session:'previous',lease:'dead',uploads:2,downloads:0,error:'No active FS RPC. ECONNREFUSED 127.0.0.1:36287'};const calls=[];
 const backend={async request(method,data){calls.push(method);if(method==='status')return {...state};if(method==='start-storage'){starts++;state={...state,status:'ready',mode:'storage',session:'current',lease:'live',error:undefined};return {...state};}if(method==='browse')return {cwd:data.path,entries:[{name:'disc.cue',type:'file',size:4}],writable:true};throw Error('Unexpected mutation '+method);}};
 const services={async listenHttp(server){server.listen(0,'127.0.0.1');await once(server,'listening');return {origin:'http://127.0.0.1:'+server.address().port};}};
 const handle=await create({root:'/unused',services,backend,guard:async()=>({ready:true,session:'current'})});const u=new URL(handle.url);
 try{for(let i=0;i<2;i++){const r=await fetch(u.origin+'/api/connect',{method:'POST',headers:{Origin:u.origin,'x-izpsx-key':u.hash.slice(1),'content-type':'application/json'},body:'{"area":"games"}'}),result=await r.json();assert.equal(r.status,200,result.error);assert.equal(result.entries[0].name,'disc.cue');}assert.equal(starts,1);assert(!calls.some(m=>['stop','sd-upload-chunk','sd-delete','sd-upload-cancel'].includes(m)));}finally{await handle.close();}
});

test('PS1 opens SD storage on entry without opening or requiring the Super File Bros. window',async()=>{
 const fs=require('node:fs'),path=require('node:path'),vm=require('node:vm'),source=fs.readFileSync(path.join(__dirname,'../server/ui/app.js'),'utf8');
 for(const ready of [true,false]){let browses=0;const nodes=new Map(),context={ready,status:async()=>{},action:async fn=>fn(),browse:async()=>{browses++;},$:id=>{if(!nodes.has(id))nodes.set(id,{dataset:{},replaceChildren(){}});return nodes.get(id);}};
 const initialize=vm.runInNewContext(source.slice(source.indexOf('async function initialize(){'),source.indexOf('\ninitialize().catch'))+'\ninitialize;',context);await initialize();assert.equal(browses,ready?1:0);}
});

test('PS1 clears session-scoped connection errors and ignores a superseded failing status reply',async()=>{
 const fs=require('node:fs'),path=require('node:path'),vm=require('node:vm'),source=fs.readFileSync(path.join(__dirname,'../server/ui/app.js'),'utf8'),nodes=new Map(),pending=[];
 const context={statusSequence:0,connectionSession:'old',relative:'old-folder',cached:{entries:['old']},uploading:false,controls(){},$:id=>{if(!nodes.has(id))nodes.set(id,{dataset:{source:'status'},textContent:'Old error',replaceChildren(){this.cleared=true;}});return nodes.get(id);},api:()=>new Promise((resolve,reject)=>pending.push({resolve,reject}))};
 const status=vm.runInNewContext(source.slice(source.indexOf('async function status()'),source.indexOf('\nasync function upload('))+'\nstatus;',context);
 const old=status(),fresh=status();pending[1].resolve({ready:true,attached:true,session:'new',pendingUpload:null});await fresh;pending[0].reject(Error('Old endpoint refused'));await old;
 assert.equal(context.connectionSession,'new');assert.equal(context.cached,null);assert.equal(context.relative,'');assert.equal(nodes.get('files').cleared,true);assert.equal(nodes.get('notice').textContent,'');
 const ended=status();pending[2].resolve({ready:false,attached:false,session:null,pendingUpload:null});await ended;assert.equal(context.connectionSession,null);
});

test('an ongoing upload never masquerades as paused or offers a second concurrent resume',async()=>{
 const services={async listenHttp(server){server.listen(0,'127.0.0.1');await once(server,'listening');return {origin:'http://127.0.0.1:'+server.address().port};}},backend={async request(method,data){if(method==='status')return {status:'ready',mode:'storage',session:'current',lease:'live'};if(method==='sd-mkdir')return {ok:true};if(method==='sd-upload-begin')return {token:'upload',bytes:0,chunkBytes:458752};if(method==='sd-upload-chunk')return {bytes:3};if(method==='sd-upload-pause')return {ok:true};throw Error('Unexpected '+method);}};
 const h=await create({root:'/unused',services,backend,guard:async()=>({ready:true,session:'current'})}),u=new URL(h.url);
 const request=async(route,data)=>{const r=await fetch(u.origin+route,{method:data?'POST':'GET',headers:{origin:u.origin,'x-izpsx-key':u.hash.slice(1),'content-type':'application/json'},...(data?{body:JSON.stringify(data)}:{})});return {status:r.status,value:await r.json()};};
 try{await request('/api/upload/begin',{area:'games',name:'disc.bin',size:6,fingerprint:'same'});await request('/api/upload/chunk',{token:'upload',offset:0,data:'AQID'});const active=(await request('/api/status')).value.pendingUpload;assert.equal(active.active,true);assert.equal(active.resumeAllowed,false);assert.equal(active.offset,3);assert.equal(active.reason,'Upload in progress.');assert.equal((await request('/api/upload/resume',{token:'upload'})).status,400);await request('/api/upload/pause',{token:'upload'});const paused=(await request('/api/status')).value.pendingUpload;assert.equal(paused.active,false);assert.equal(paused.resumeAllowed,true);}finally{await h.close();}
});

test('resume failures stay next to the Continue button instead of being overwritten by a paused status',async()=>{
 const fs=require('node:fs'),path=require('node:path'),vm=require('node:vm'),source=fs.readFileSync(path.join(__dirname,'../server/ui/app.js'),'utf8'),nodes=new Map(),resume={token:'resume',name:'folder/disc.bin',bytes:4,fingerprint:'2a',area:'games'},file={name:'disc.bin',size:4,slice:()=>({arrayBuffer:async()=>Uint8Array.of(1,2,3,4).buffer})};let cleared=false;
 const context={crypto:{subtle:{digest:async()=>Uint8Array.of(42).buffer}},DataView,BigInt,Uint8Array,area:'games',relative:'',uploadToken:'resume',pendingUpload:resume,controls(){},size:String,setInterval:()=>99,clearInterval:()=>{cleared=true;},$:id=>{if(!nodes.has(id))nodes.set(id,{removeAttribute(){}});return nodes.get(id);},api:async()=>{throw Error('The file service is busy');},status:async()=>{context.$('upload-detail').textContent='Upload paused';},browse:async()=>{}};
 const upload=vm.runInNewContext(source.slice(source.indexOf('async function upload('),source.indexOf("\n$('connect')"))+'\nupload;',context);await assert.rejects(upload([file],false,resume),/file service is busy/);assert.equal(cleared,true);assert.match(nodes.get('upload-detail').textContent,/file service is busy/);assert.equal(context.uploadToken,'resume');
});
