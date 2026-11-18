const {parentPort}=require('node:worker_threads'),fs=require('node:fs'),path=require('node:path'),codec=require('./pc-codec.js');
codec.installWasm(fs.readFileSync(path.join(__dirname,'frame-codec.wasm')));
parentPort.on('message',f=>{try{const start=performance.now();const bytes=codec.encode(f.rgba,f.w,f.h,1,{bgra:!!f.bgra});parentPort.postMessage({bytes,w:f.w,h:f.h,format:1,encodeMs:performance.now()-start},[bytes.buffer]);}catch(e){parentPort.postMessage({error:e.message});}});
