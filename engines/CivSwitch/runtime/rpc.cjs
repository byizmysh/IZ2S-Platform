'use strict';
const {create}=require('./client.cjs');
// Adapt only the explicit allocator/memory operations used by the shared
// renderer. Numeric SB4 SDK offsets never reach the console as addresses.
const sdkAliases={5732112:['sdk','posix_memalign'],5616480:['main','free'],5797280:['sdk','memset'],5795760:['sdk','memcpy']};
function connect({host,addresses,graphics,client=create({host})}){
  const bindings=new Map(),verified=new Set();
  const ready=client.connect().then(info=>{
    for(const name of ['main','sdk'])if(Number(info.modules.find(m=>m.name===name)?.base)!==addresses[name])throw Error('CivSwitch process changed; start a fresh graphics session');
  });
  // A client can be created before its first awaited RPC. Observe this rejection
  // immediately so stale process metadata cannot become an unhandled rejection.
  ready.catch(()=>{});
  async function bind(module,name){
    await ready;
    const key=module+':'+name;
    if(!bindings.has(key))bindings.set(key,await client.lookup(name,module));
    return bindings.get(key);
  }
  async function callBound(module,name,args){await bind(module,name);return {result:await client.call(name,args,module),output:Buffer.alloc(0)};}
  const rpc={get uncertain(){return client.uncertain;},a:addresses,output:addresses.scratch+0x4800,nativeABI:'civswitch',
    read:async(address,size)=>{await ready;return client.read(address,size);},
    write:async(address,bytes)=>{
      await ready;const data=Buffer.from(bytes);
      // GPU storage starts zeroed. Fill uniform blocks on the console rather
      // than transmitting megabytes of identical bytes over the JS loader.
      if(data.length>=65536&&data.every(value=>value===data[0])){
        await client.validateOwned(address,data.length);
        const result=await callBound('sdk','memset',[address,data[0],data.length]);
        if(result.result!==BigInt(address))throw Error('CivSwitch native fill failed');return;
      }
      if(data.length>=65536){await client.streamWrite(address,data);return;}
      for(let offset=0;offset<data.length;offset+=262144){const part=data.subarray(offset,offset+262144);if(part.length>=16384)await client.bulkWrite(BigInt(address)+BigInt(offset),part);else await client.write(BigInt(address)+BigInt(offset),part);}
    },
    initializeOwned:async(address,size)=>{
      await ready;await client.validateOwned(address,size);
      const result=await callBound('sdk','memset',[address,0,size]);
      if(result.result!==BigInt(address))throw Error('CivSwitch owned buffer clear failed');
    },
    query:async address=>{await ready;const r=await client.query(address);return {...r,address:Number(r.address),size:Number(r.size)};},
    call:async(offset,args=[])=>{
      const alias=sdkAliases[offset];if(!alias)throw Error('Shared renderer requested an unsupported SB4 SDK operation: '+offset);
      const result=await callBound(alias[0],alias[1],args);
      if(alias[1]==='posix_memalign'&&Number(args[0])===rpc.output)result.output=await rpc.read(rpc.output,8);
      return result;
    },
    verifySdk:async names=>{
      const aliases={allocate:['sdk','posix_memalign'],free:['main','free'],memset:['sdk','memset']};
      for(const name of names){const alias=aliases[name];if(!alias)throw Error('Unknown CivSwitch SDK operation');await bind(...alias);}
    },
    async callAddress(address,args=[]){
      if(!verified.has(Number(address)))throw Error('Verify the graphics export before calling it');
      const entry=graphics.symbols.find(s=>s.section&&s.type===2&&graphics.base+s.offset===Number(address));
      if(!entry)throw Error('Native address is not an exported graphics function');
      return callBound('graphics',entry.name,args);
    },
    async request(body){
      if(body.command==='status'){await ready;return {ready:true};}
      if(body.command!=='verify-function')throw Error('Unsupported shared CivSwitch renderer request: '+body.command);
      const entry=graphics.symbols.find(s=>s.section&&s.type===2&&graphics.base+s.offset===Number(body.address));
      if(!entry||typeof body.hex!=='string'||!body.hex.length||!entry.size||body.hex.length>512||!/^[0-9a-f]+$/.test(body.hex))throw Error('Invalid graphics function verification');
      const actual=await rpc.read(body.address,body.hex.length/2);
      if(actual.toString('hex')!==body.hex)throw Error('Graphics export changed: '+entry.name);
      const live=await bind('graphics',entry.name);
      if(Number(live.address)!==Number(body.address))throw Error('Cached graphics symbol moved: '+entry.name);
      verified.add(Number(body.address));return {verified:true};
    },
    validateApp:async app=>{
      if(app.manifest.native||app.manifest.localRuntime)throw Error('This native application needs the Civ7 native executor; its SB4 executor cannot be reused.');
      if(app.manifest.builtin||app.manifest.shellContribution)throw Error('This menu extension needs its Civ7 host service before it can be used.');
    },
    createNativeClient:async(definitions,bytes)=>{await ready;return require('./audio-client.cjs').create(rpc,client,definitions,bytes);},
    setAudioClockMode:async mode=>{await ready;return client.audioClockMode(mode);},
    configureAudioQueue:async options=>{await ready;return client.configureAudioQueue(options);},
    detachAudioQueue:async()=>{await ready;return client.detachAudioQueue();},
    close:async()=>{await ready;if(!client.uncertain)await client.releaseNative();},
  };
  return rpc;
}
module.exports={connect,sdkAliases};
