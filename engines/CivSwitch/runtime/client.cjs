'use strict';
// CivSwitch evaluates one script per half-closed TCP connection. A request
// whose reply is lost must never be repeated automatically: native calls may
// already have completed on the console.
const net = require('node:net'), crypto = require('node:crypto'),zlib=require('node:zlib');
const fs = require('node:fs/promises'), path = require('node:path');
function exchange({host, port = 9026, timeoutMs = 15000}, code) {
  if (!net.isIPv4(host) || !Number.isInteger(port) || port < 1 || port > 65535)
    return Promise.reject(Error('Invalid CivSwitch endpoint'));
  if (Buffer.byteLength(code) >= 500 * 1024) return Promise.reject(Error('CivSwitch script exceeds loader capacity'));
  return new Promise((resolve, reject) => {
    let output = '', settled = false, submitted = false;
    const socket = net.createConnection({host, port});
    const finish = error => {
      if (settled) return;
      settled = true; clearTimeout(timer); socket.destroy();
      if (error) {error.uncertain = submitted; reject(error);} else resolve(output);
    };
    const timer = setTimeout(() => finish(Error('CivSwitch reply timed out; request will not be replayed')), timeoutMs);
    socket.once('connect', () => {submitted = true; socket.end(code);});
    socket.on('data', bytes => {
      output += bytes.toString('utf8');
      if (output.length > 1024 * 1024) finish(Error('CivSwitch reply exceeds its bound'));
    });
    socket.once('error', finish);
    socket.once('end', () => finish());
    socket.once('close', () => {if (!settled) finish(Error('CivSwitch connection closed before reply completed'));});
  });
}
function create(options) {
  const send = options.exchange || (code => exchange(options, code));
  let tail = Promise.resolve(), uncertain = false, installed = false;
  function serial(action) {
    const job = tail.then(action); tail = job.catch(() => {}); return job;
  }
  async function request(command, body = {}, {bootstrap = false,bulk = null,timeoutMs} = {}) {
    if (uncertain && !['info', 'read'].includes(command)) throw Error('CivSwitch execution is uncertain; reconnect and inspect before making another native call');
    const marker = 'IZ2S_RPC_' + crypto.randomBytes(12).toString('hex') + '=';
    const prefix = bootstrap ? (await Promise.all(['payload.js','native.js','audio-clock.js','audio-queue.js','frames.js','stream.js'].map(name=>fs.readFile(path.join(__dirname,name),'utf8')))).join('\n') : '';
    // Upstream log() writes into a 2 KiB native buffer without a length check.
    // Escape Unicode and frame every reply into bounded ASCII messages.
    const commandBody=JSON.stringify({...body,command});
    const invoke=command==='frame'?'window.__iz2sCivRuntime.frame('+JSON.stringify(body.plan)+','+JSON.stringify(body.count)+')':'window.__iz2sCivRuntime.dispatch('+(command==='bulk-write'?commandBody.slice(0,-1)+',"received":total_read}':commandBody)+')';
    let code = prefix + '\n(async()=>{let reply;try{reply={ok:true,value:await '+invoke+'};}catch(e){reply={ok:false,error:String(e.message||e),uncertain:e.uncertain===true};}const text=JSON.stringify(reply).replace(/[^\\x20-\\x7e]/g,c=>"\\\\u"+c.charCodeAt(0).toString(16).padStart(4,"0"));const count=Math.ceil(text.length/768);for(let i=0;i<count;i++){const line=' + JSON.stringify(marker) + '+i+"/"+count+"="+text.slice(i*768,(i+1)*768);if(window.__iz2sCivRuntime.emit)window.__iz2sCivRuntime.emit(line+"\\n");else await log(line);}const done=' + JSON.stringify(marker+'DONE') + ';if(window.__iz2sCivRuntime.emit)window.__iz2sCivRuntime.emit(done+"\\n");else await log(done);})()';
    if(bulk!==null){if(Buffer.byteLength(code)>8190)throw Error('Bulk header exceeds its slot');code=code.padEnd(8190,' ')+'/*'+bulk+'*/';}
    let output;
    try {output = await (timeoutMs&&!options.exchange?exchange({...options,timeoutMs},code):send(code));} catch (error) {if (error.uncertain) uncertain = true; throw error;}
    const allLines = output.split(/\r?\n/), lines = allLines.filter(value => value.startsWith(marker) && value !== marker+'DONE');
    if (!lines.length || !allLines.includes(marker+'DONE')) {
      uncertain = true;
      throw Object.assign(Error('CivSwitch did not confirm the complete request; no automatic retry'), {uncertain:true});
    }
    let reply;
    try {
      let total = null;
      const parts = new Map();
      for (const line of lines) {
        const match = /^(\d+)\/(\d+)=(.*)$/.exec(line.slice(marker.length));
        if (!match || +match[2] > 1500 || +match[2] < 1 || +match[1] >= +match[2] ||
            total !== null && total !== +match[2] || parts.has(+match[1])) throw Error('Invalid reply frame');
        total = +match[2]; parts.set(+match[1],match[3]);
      }
      if (parts.size !== total) throw Error('Incomplete reply frames');
      reply = JSON.parse(Array.from({length:total},(_,i)=>parts.get(i)).join(''));
    } catch {
      uncertain = true; throw Object.assign(Error('Invalid CivSwitch reply'), {uncertain:true});
    }
    if (!reply.ok) {if(reply.uncertain)uncertain=true;throw Object.assign(Error(reply.error),reply.uncertain?{uncertain:true,resourcesRetained:true}:{});}
    return reply.value;
  }
  return {
    get uncertain() {return uncertain;},
    connect: () => serial(async () => {
      const info = await request('info', {}, {bootstrap:!installed});
      if (info.abi !== 1 || info.engine !== 'civswitch') throw Error('Unsupported CivSwitch native bridge');
      installed = true; return info;
    }),
    info: () => serial(() => request('info')),
    quiet: enabled => serial(() => request('quiet', {enabled})),
    audioClockMode:mode=>serial(()=>request('audio-clock-mode',{mode})),
    configureAudioQueue:options=>serial(()=>request('audio-queue-open',options)),
    detachAudioQueue:()=>serial(()=>request('audio-queue-detach')),
    notice: text => serial(() => request('notice', {text})),
    lookup: (name, module='sdk') => serial(() => request('lookup', {name,module})),
    resolveImport: (name, module='main') => serial(() => request('resolve-import', {name,module})),
    call: (name, args=[], module='sdk') => serial(() => request('call', {name,module,args:args.map(String)})).then(BigInt),
    query: address => serial(() => request('query', {address:String(address)})).then(value=>({...value,address:BigInt(value.address),size:BigInt(value.size)})),
    releaseNative: () => serial(() => request('release-native')),
    registerModule: (name,address) => serial(() => request('register-module', {name,address:String(address)})),
    frame: (plan,count=2) => serial(() => request('frame', {plan,count})),
    read: (address, size) => serial(async () => {
      if (!Number.isSafeInteger(size) || size < 0 || size > 8 * 1024 * 1024) throw Error('Invalid memory read length');
      const chunks = [];
      for (let offset = 0; offset < size; offset += 32768) {
        const length = Math.min(32768, size-offset);
        const hex = await request('read', {address:String(BigInt(address)+BigInt(offset)), size:length});
        if (typeof hex !== 'string' || hex.length !== length*2 || !/^[0-9a-f]*$/.test(hex)) throw Error('Invalid memory read response');
        chunks.push(Buffer.from(hex, 'hex'));
      }
      return Buffer.concat(chunks);
    }),
    allocate: size => serial(() => request('allocate', {size})).then(BigInt),
    bulkWrite:(address,bytes)=>serial(async()=>{
      const data=Buffer.from(bytes);if(!data.length||data.length>4*1024*1024)throw Error('Invalid bulk write size');
      const packed=zlib.deflateSync(data),compressed=packed.length<data.length*.9,source=compressed?packed:data;
      if(source.length>360*1024)throw Error('Bulk source exceeds loader capacity');
      return request('bulk-write',{address:String(address),size:data.length,sourceSize:source.length,encoding:compressed?'zlib':'raw',offset:8192},{bulk:source.toString('base64')});
    }),
    streamWrite:(address,bytes)=>serial(()=>require('./stream.cjs').write(options,body=>request('stream-write',body,{timeoutMs:120000}),address,bytes)),
    cancelWriteRange:(address,size)=>serial(()=>request('cancel-write-range',{address:String(address),size})),
    validateOwned:(address,size)=>serial(()=>request('validate-owned',{address:String(address),size})),
    free: address => serial(() => request('free', {address:String(address)})),
    write: (address, bytes) => serial(async () => {
      const data = Buffer.from(bytes);
      if (data.length > 8*1024*1024) throw Error('Memory write exceeds its bound');
      if(!data.length){await request('write',{address:String(address),hex:''});return;}
      for (let offset = 0; offset < data.length; offset += 32768)
        await request('write', {address:String(BigInt(address)+BigInt(offset)), hex:data.subarray(offset,offset+32768).toString('hex')});
    }),
  };
}
module.exports = {create, exchange};
