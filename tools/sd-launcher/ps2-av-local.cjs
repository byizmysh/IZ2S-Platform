'use strict';
const fs=require('node:fs/promises'),path=require('node:path'),assert=require('node:assert/strict'),crypto=require('node:crypto'),{execFileSync}=require('node:child_process');
const root=path.resolve(__dirname,'../..'),sha=b=>crypto.createHash('sha256').update(b).digest('hex');
async function main(){
 const out=path.join(root,'data/build-cache/sd-ps2-av-local',new Date().toISOString().replace(/[:.]/g,'-')+'-'+process.pid),logs=[];await fs.mkdir(out,{recursive:true});
 const env={...process.env,TMPDIR:out,ASAN_OPTIONS:'detect_leaks=0',UBSAN_OPTIONS:'halt_on_error=1'};
 function run(command,args){try{const output=execFileSync(command,args,{cwd:root,env,encoding:'utf8',timeout:60000,stdio:['ignore','pipe','pipe']});logs.push({command,args,output});return output;}catch(e){logs.push({command,args,failed:true,status:e.status,stdout:String(e.stdout||''),stderr:String(e.stderr||'')});throw e;}}
 try{
  const profile=JSON.parse(await fs.readFile(path.join(root,'core/console/sd-launcher/ps2/av-profile.json')));for(const [file,digest]of Object.entries(profile.sourcePins))assert.equal(sha(await fs.readFile(path.join(root,file))),digest,'Exact AV reference changed: '+file);
  const moduleDirectory=path.join(root,'modules',profile.moduleId,'.versions',profile.moduleSha256),C=require(path.join(moduleDirectory,'server/config.cjs')),V=require(path.join(root,'data/ps2-cold-work/build/display-composed-aspect-v1/prepare_native_video.cjs'));
  const source='core/console/sd-launcher/ps2/av.c',test='tests/sd-launcher/ps2-av-model.c',model=path.join(out,'model');run('cc',['-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fno-builtin','-fsanitize=address,undefined',test,source,'-o',model]);const schema=JSON.parse(run(model,['--schema']));
  const inputs=[],expected=[],vectors=[],session='local-av-contract';
  for(let i=0;i<16;i++){
   const input=Buffer.alloc(schema.bytes),o=schema.offsets,s=schema.sourceOffsets,n=1+i%8,w=(at,v)=>input.writeBigUInt64LE(BigInt(v),at),vulkan=0x1000000000+i*0x40000000;
   let at=vulkan+45989888+4096;const reserve=bytes=>{const p=at;at+=(bytes+4095)&~4095;at+=4096;return p;};
   const avPage=reserve(4096),audio=reserve(36864),scratchBytes=4325376+(i&1?4096:0),scratch=reserve(scratchBytes),mapped=[reserve(4198400),reserve(4198400)],protectedSpans=[];
   for(let j=0;j<(i===15?24:4);j++)protectedSpans.push({base:reserve(4096*(j+1)),bytes:4096*(j+1)});
   const resources={device:0x30000000+i*4096,queue:0x31000000+i*4096,swapchain:(1n<<63n)+BigInt(i+1),images:Array.from({length:n},(_,k)=>k+1)},token=Buffer.alloc(16);token.writeBigUInt64LE(BigInt(i+1));token.writeBigUInt64LE(0x8123456700000000n+BigInt(i),8);
   input.writeUInt32LE(1,o.version);input.writeUInt32LE(schema.bytes,o.bytes);input.writeUInt32LE(n,o.image_count);for(const [key,value]of Object.entries({av_page:avPage,vulkan,device:resources.device,queue:resources.queue,swapchain:resources.swapchain,audio,scratch,scratch_bytes:scratchBytes}))w(o[key],value);token.copy(input,o.token);
   input.writeUInt32LE(protectedSpans.length,o.protected_count);protectedSpans.forEach((span,j)=>{w(o.protected+j*16,span.base);w(o.protected+j*16+8,span.bytes);});
   const owners=[],leases=[];
   for(let slot=0;slot<2;slot++){
    const b=o.sources+slot*schema.sourceBytes,start=(1n<<62n)+BigInt(i*0x1000+slot*0x100),object={};
    for(const [j,key]of ['buffer','memory','set_layout','layout','pool','descriptor'].entries()){object[key]=start+BigInt(j+1);w(b+s[key],object[key]);}
    w(b+s.mapped,mapped[slot]);w(b+s.mapped_bytes,4198400);const flags=i<8?6:15;input.writeUInt32LE(flags,b+s.memory_flags);
    const commands=Array.from({length:n},(_,j)=>start+BigInt(32+j)),semaphores=Array.from({length:n+1},(_,j)=>start+BigInt(64+j));commands.forEach((v,j)=>w(b+s.commands+j*8,v));semaphores.forEach((v,j)=>w(b+s.semaphores+j*8,v));
    const storage={buffer:object.buffer,memory:object.memory,setLayout:object.set_layout,layout:object.layout,descriptorPool:object.pool,descriptor:object.descriptor,mapped:mapped[slot],bytes:4198400,allocationBytes:4198400,memoryFlags:flags},gpu={ready:true,scratchRestored:true,privateStorage:storage,commands:[commands]},owner={session,token:token.toString('hex'),retained:true,restored:false,knownIdle:false,gpu,semaphores,vulkanRx:vulkan,vulkanRxBytes:45989888};
    leases.push(V.packLease({token,resources,gpu,semaphores}));owners.push(owner);
   }
   owners[0].pair={format:'ps2-native-present-pair-prepare/1',slots:2,secondaryLeaseHex:leases[1].toString('hex'),secondaryOwner:owners[1]};
   const page=C.av({vulkanRx:vulkan,vulkanRxBytes:45989888,audio,scratch,scratchBytes,lease:leases[0],videoOwner:owners[0],session,avPage,allocations:[{label:'ps2-av-description',address:avPage,bytes:4096}]});assert.equal(page.length,4096);
   inputs.push(input);expected.push(page);vectors.push({imageCount:n,memoryFlags:i<8?6:15,protectedSpans:protectedSpans.length,inputSha256:sha(input),pageSha256:sha(page)});
  }
  const inputFile=path.join(out,'inputs.bin'),expectedFile=path.join(out,'expected.bin');await fs.writeFile(inputFile,Buffer.concat(inputs));await fs.writeFile(expectedFile,Buffer.concat(expected));
  const linker=path.join(out,'layout.ld');await fs.writeFile(linker,'ENTRY(iz_ps2_pack_av)\nSECTIONS { . = 0; .text : { KEEP(*(.text.iz_ps2_pack_av)) *(.text*) *(.rodata*) } /DISCARD/ : { *(.comment*) *(.note*) *(.eh_frame*) } }\n');const binaries={};
  for(const target of ['x86_64-none-elf','aarch64-none-elf']){
   const object=path.join(out,target+'.o'),elf=path.join(out,target+'.elf'),binary=path.join(out,target+'.bin'),flags=['--target='+target,'-std=c11','-Os','-ffreestanding','-fno-builtin','-fPIC','-fno-stack-protector','-fno-unwind-tables','-fno-asynchronous-unwind-tables','-fno-jump-tables','-Wall','-Wextra','-Werror'];if(target.startsWith('aarch64'))flags.push('-mbranch-protection=bti','-mgeneral-regs-only','-mno-outline-atomics');
   run('clang',[...flags,'-c',source,'-o',object]);require('../../core/console/native/pic.cjs').verifyObject(object);run('ld.lld',['-T',linker,'--no-undefined','--build-id=none',object,'-o',elf]);assert.match(run('llvm-readelf',['-r',elf]),/There are no relocations/);assert.equal(run('llvm-nm',['--undefined-only',elf]).trim(),'');run('llvm-objcopy',['-O','binary',elf,binary]);const bytes=await fs.readFile(binary);binaries[target]={file:binary,bytes:bytes.length,sha256:sha(bytes)};
  }
  const result=JSON.parse(run(model,[inputFile,expectedFile,binaries['x86_64-none-elf'].file,'16']));assert(result.passed&&result.actualHostRX&&result.invalidCalls>2000);
  const report={format:'iz-ps2-native-av-local-v1',passed:true,at:new Date().toISOString(),...result,profile,vectors,schema,binaries,outputDirectory:out,sourceSha256:{},hardwareQualified:false,engineExecuted:false,gpuExecuted:false,change:'Native construction of the original paired PS2 video/AV page, including two independent source graphs, per-image commands/semaphores and all original extents',status:'local-ABI-differential-passed',metric:'AV descriptor bytes',before:{nativeProducer:false},after:{nativeProducer:true,pageBytes:4096,sourceCount:2,sourceBytesEach:4198400,vectors:16,invalidCallsRejectedWithoutWrites:result.invalidCalls},scenario:'Exact frozen R176 AV and pair packers, 1..8 swapchain images, coherent flags 6/15, optional larger scratch and 24 protected spans',environment:'ASan/UBSan, linked C and actual host RX execution; resource identities and capabilities modeled',performanceGainPercent:null,evidence:[path.relative(root,inputFile),path.relative(root,expectedFile)],limitations:['Performance improvement has not been measured','This pure packer grants no allocation, mapping, execution or cleanup authority','The original engine and display shaders are unchanged and not executed by this check','Native GPU graph creation, engine controller and engine-specific post-entry cleanup remain to be connected']};
  for(const file of [source,'core/console/sd-launcher/ps2/av.h','core/console/sd-launcher/ps2/av-profile.json','core/console/sd-launcher/ps2/gpu-profile.h',test,'tools/sd-launcher/ps2-av-local.cjs'])report.sourceSha256[file]=sha(await fs.readFile(path.join(root,file)));
  await fs.writeFile(path.join(out,'report.json'),JSON.stringify(report,null,2)+'\n');const file=path.join(root,'data/diagnostics/sd-ps2-native-av-local.json');await fs.writeFile(file,JSON.stringify(report,null,2)+'\n');console.log(JSON.stringify({passed:true,report:file,vectors:result.vectors,invalidCalls:result.invalidCalls,armBytes:binaries['aarch64-none-elf'].bytes,consoleCommands:0}));return report;
 }finally{await fs.writeFile(path.join(out,'commands.json'),JSON.stringify(logs,null,2)+'\n');}
}
module.exports={main};if(require.main===module)main().catch(e=>{console.error(e.message.split('\n').slice(0,18).join('\n'));process.exitCode=1;});
