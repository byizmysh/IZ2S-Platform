const fs=require('node:fs/promises'),path=require('node:path'),crypto=require('node:crypto'),zlib=require('node:zlib');
const MAGIC=Buffer.from('IZCAP11\0'),HEADER=80,MAX=64*1024*1024,MAX_FILES=128,EXTENSION='.izs';
const check=(v,m)=>{if(!v)throw Error(m);};
function safeName(name){check(typeof name==='string'&&name.length>0&&name.length<256&&!name.includes('\\')&&!name.includes('\0')&&!name.includes(':')&&!path.isAbsolute(name)&&name.split('/').every(p=>p&&p!=='.'&&p!=='..'),'Invalid capsule asset path');return name;}
async function key(file){try{const b=await fs.readFile(file);check(b.length===32,'Invalid capsule key');return b;}catch(e){if(e.code!=='ENOENT')throw e;await fs.mkdir(path.dirname(file),{recursive:true,mode:0o700});const b=crypto.randomBytes(32);try{await fs.writeFile(file,b,{flag:'wx',mode:0o600});return b;}catch(e){if(e.code==='EEXIST')return key(file);throw e;}}}
function encode(metadata,files,secret){
 check(Buffer.isBuffer(secret)&&secret.length===32,'Invalid capsule key');check(files.length>0&&files.length<=MAX_FILES,'Invalid asset count');
 let offset=0;const names=new Set(),table=files.map(f=>{const name=safeName(f.name);check(!names.has(name)&&Buffer.isBuffer(f.bytes)&&f.bytes.length>0,'Invalid or duplicate capsule asset');names.add(name);const row={path:name,offset,bytes:f.bytes.length,sha256:crypto.createHash('sha256').update(f.bytes).digest('hex')};offset+=f.bytes.length;return row;});
 const json=Buffer.from(JSON.stringify({...metadata,format:'iz-capsule',version:1,files:table}));check(json.length<=65536,'Capsule metadata too large');
 const size=Buffer.alloc(4);size.writeUInt32LE(json.length);const plain=Buffer.concat([size,json,...files.map(f=>f.bytes)]);check(plain.length<=MAX,'Capsule exceeds 64 MiB');
 const packed=zlib.brotliCompressSync(plain,{params:{[zlib.constants.BROTLI_PARAM_QUALITY]:7}}),header=Buffer.alloc(HEADER);MAGIC.copy(header);header.writeUInt32LE(1,8);header.writeUInt32LE(packed.length,12);header.writeUInt32LE(plain.length,16);crypto.randomBytes(12).copy(header,20);crypto.createHash('sha256').update(plain).digest().copy(header,48);
 const cipher=crypto.createCipheriv('aes-256-gcm',secret,header.subarray(20,32));cipher.setAAD(Buffer.concat([header.subarray(0,32),header.subarray(48)]));const encrypted=Buffer.concat([cipher.update(packed),cipher.final()]);cipher.getAuthTag().copy(header,32);return Buffer.concat([header,encrypted]);
}
function decode(bytes,secret){
 check(Buffer.isBuffer(secret)&&secret.length===32,'Invalid capsule key');check(Buffer.isBuffer(bytes)&&bytes.length>=HEADER&&bytes.length<=MAX+HEADER+65536,'Invalid capsule size');
 check(bytes.subarray(0,8).equals(MAGIC)&&bytes.readUInt32LE(8)===1,'Unsupported IZ Capsule format');const length=bytes.readUInt32LE(12),expanded=bytes.readUInt32LE(16);check(length===bytes.length-HEADER&&expanded>4&&expanded<=MAX,'Invalid capsule length');
 let packed;try{const cipher=crypto.createDecipheriv('aes-256-gcm',secret,bytes.subarray(20,32));cipher.setAAD(Buffer.concat([bytes.subarray(0,32),bytes.subarray(48,80)]));cipher.setAuthTag(bytes.subarray(32,48));packed=Buffer.concat([cipher.update(bytes.subarray(HEADER)),cipher.final()]);}catch{throw Error('Capsule authentication failed: wrong key or damaged file');}
 const plain=zlib.brotliDecompressSync(packed,{maxOutputLength:expanded});check(plain.length===expanded&&crypto.timingSafeEqual(crypto.createHash('sha256').update(plain).digest(),bytes.subarray(48,80)),'Capsule integrity check failed');
 const jsonBytes=plain.readUInt32LE();check(jsonBytes>0&&jsonBytes<=65536&&jsonBytes+4<=plain.length,'Invalid capsule metadata');const metadata=JSON.parse(plain.subarray(4,4+jsonBytes));check(metadata.format==='iz-capsule'&&metadata.version===1&&/^[a-z0-9][a-z0-9-]{0,47}$/.test(metadata.id)&&typeof metadata.name==='string'&&metadata.name.length<=64,'Invalid capsule identity');
 check(Array.isArray(metadata.files)&&metadata.files.length>0&&metadata.files.length<=MAX_FILES,'Invalid capsule asset table');const payload=plain.subarray(4+jsonBytes),names=new Set();let at=0;
 const files=metadata.files.map(f=>{safeName(f.path);check(!names.has(f.path)&&f.offset===at&&Number.isSafeInteger(f.bytes)&&f.bytes>0&&at+f.bytes<=payload.length,'Invalid capsule asset range');names.add(f.path);const data=payload.subarray(at,at+f.bytes);at+=f.bytes;check(crypto.createHash('sha256').update(data).digest('hex')===f.sha256,'Capsule asset hash differs');return {name:f.path,bytes:data};});check(at===payload.length&&names.has('app.json'),'Incomplete capsule payload');
 return {metadata,files,expandedBytes:plain.length,packageBytes:bytes.length};
}
async function pack(directory,secret,{validate=true}={}){
 let descriptor;try{descriptor=JSON.parse(await fs.readFile(path.join(directory,'module.json'),'utf8'));}catch(e){if(e.code!=='ENOENT')throw e;}
 if(descriptor){check(descriptor.format==='izlauncher.module'&&descriptor.switch?.source==='switch','Invalid module source');directory=path.join(directory,'switch');}
 const catalog=require('../services/app-catalog.cjs'),app=await catalog.readApp(directory,{validate}),m=app.manifest;
 const names=new Set(['app.json',m.icon,m.initialState,m.data,m.memory?.initial,...Object.values(m.shaders||{}),...Object.values(m.audio?.effects||{}),m.computeSequence?.begin,m.computeSequence?.after,...(m.run11Modules||[]),...(m.localRuntime?.assets||[]).map(a=>a.file)].filter(Boolean));
 const files=[];for(const name of [...names].sort()){const file=await catalog.asset(app.root,safeName(name),MAX);files.push({name,bytes:file.bytes});}
 return encode({id:app.id,name:app.name,appVersion:app.version,description:app.description,kind:m.builtin?'builtin':'gpu-app'},files,secret);
}
module.exports={MAGIC,HEADER,MAX,EXTENSION,key,encode,decode,pack,safeName};
