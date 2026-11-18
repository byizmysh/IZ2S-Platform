// Qualify existing AArch64 zlib functions only after the unmodified menu is up.
const fs=require('node:fs/promises'),path=require('node:path'),zlib=require('node:zlib');
const definitions=require('./platform.cjs').require('core/console/abi/zlib-functions.json'),{crc32}=require('./block-delta.cjs');
const signed=x=>Number(BigInt.asIntN(32,x)),check=(v,m)=>{if(!v)throw Error(m);};
async function create({rpc,directory}){
 const phase=await require('./platform.cjs').require('core/services/post-menu.cjs').assertReady(rpc,directory),record={...phase,kind:'SDK raw DEFLATE dictionary self-test',ready:false,constructorProbe:false};
 const save=()=>fs.writeFile(path.join(directory,'run10-zlib.json'),JSON.stringify(record,null,2));await save();
 let api,initialized=false,ended=false;
 const call=(name,args=[])=>api.call('nnutilZlib_'+name,args),ok=async(name,args)=>check(signed(await call(name,args))===0,'SDK '+name+' failed');
 async function close(){if(ended)return;if(initialized){await ok('inflateEnd',[api.base+256]);initialized=false;}ended=true;record.closed=new Date().toISOString();await save();}
 try{
  api=await require('./platform.cjs').require('core/console/native/client.cjs').create(rpc,definitions,0x20000);record.base=api.base;const strm=api.base+256,version=Number(await call('zlibVersion'));
  check(Number.isSafeInteger(version)&&version>rpc.a.sdk&&version<rpc.a.sdk+0x2000000,'Invalid SDK zlib version pointer');
  record.version=(await rpc.read(version,16)).toString().split('\0')[0];check(/^1\.[0-9]+\.[0-9]+/.test(record.version),'Unexpected SDK zlib version');
  // Disassembly confirms version major '1' and sizeof(z_stream)==0x70.
  await ok('inflateInit2_',[strm,0xfffffff1,version,112]);initialized=true;
  const guard=Buffer.alloc(64,0xa7),dictionary=Buffer.alloc(16384);for(let i=0;i<dictionary.length;i++)dictionary[i]=(i*31+(i>>4))&255;
  const raw=Buffer.from(dictionary);raw.fill(0x5c,120,184);const input=zlib.deflateRawSync(raw,{dictionary,level:1});
  const dict=api.base+0x2000,src=api.base+0x7000,out=api.base+0x10000;
  await rpc.write(dict,dictionary);await rpc.write(src,input);await rpc.write(out-64,guard);await rpc.write(out+raw.length,guard);
  await ok('inflateSetDictionary',[strm,dict,dictionary.length]);
  const io=Buffer.alloc(48);io.writeBigUInt64LE(BigInt(src));io.writeUInt32LE(input.length,8);io.writeBigUInt64LE(BigInt(out),24);io.writeUInt32LE(raw.length,32);await rpc.write(strm,io);
  check(signed(await call('inflate',[strm,4]))===1,'SDK DEFLATE did not reach stream end');
  const state=await rpc.read(strm,48);check(state.readBigUInt64LE(40)===BigInt(raw.length)&&state.readUInt32LE(8)===0,'SDK z_stream layout/output differs');
  check((await rpc.read(out,raw.length)).equals(raw),'SDK dictionary roundtrip differs');
  check((await rpc.read(out-64,64)).equals(guard)&&(await rpc.read(out+raw.length,64)).equals(guard),'SDK output guard changed');
  check(Number(await call('crc32',[0,out,raw.length]))===crc32(raw),'SDK CRC32 differs');
  await ok('inflateReset',[strm]);record.ready=true;record.roundtripBytes=raw.length;record.compressedBytes=input.length;record.completed=new Date().toISOString();await save();
  console.log('Run10 post-menu SDK DEFLATE self-test passed.');
  return {api,strm,functions:api.functions,record,close};
 }catch(error){record.error=error.message;if(error.message==='SDK output guard changed'){record.cleanupSkipped='Guard violation; no further SDK codec calls';error.resourcesRetained=true;await save();throw error;}try{await close();}catch(e){record.cleanupError=e.message;error.resourcesRetained=true;}await save();throw error;}
}
module.exports={create,signed};
