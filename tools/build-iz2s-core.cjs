#!/usr/bin/env node
'use strict';
const fs=require('node:fs'), path=require('node:path'), os=require('node:os'), crypto=require('node:crypto'), vm=require('node:vm');
const {execFileSync}=require('node:child_process');
const sha=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
function build({engine=path.resolve(__dirname,'../engines/izmysh'), zig=process.env.ZIG||'zig'}={}) {
  engine=path.resolve(engine);
  const environment={...process.env, ZIG_GLOBAL_CACHE_DIR:process.env.ZIG_GLOBAL_CACHE_DIR||path.join(os.tmpdir(),'iz2s-zig-global'), ZIG_LOCAL_CACHE_DIR:process.env.ZIG_LOCAL_CACHE_DIR||path.join(os.tmpdir(),'iz2s-zig-local')};
  const run=(program,args)=>execFileSync(program,args,{encoding:'utf8',env:environment,stdio:['ignore','pipe','pipe']});
  if(run(zig,['version']).trim()!=='0.15.2')throw Error('IZ2S Core requires Zig 0.15.2 for a reproducible native build.');
  require(path.join(engine,'upstream/engine/check-runtime.cjs')).verify(path.join(engine,'upstream'));
  const payload=JSON.parse(run(process.execPath,[path.join(engine,'upstream/engine/build-payload.cjs'),'--zig',zig,'--check']).trim());
  const temporary=fs.mkdtempSync(path.join(os.tmpdir(),'iz2s-core-build-'));
  try {
    const directory=path.join(engine,'upstream/engine/switch2/process'), object=path.join(temporary,'test.o'), binary=path.join(temporary,'test.bin');
    run(zig,['cc','-target','aarch64-freestanding','-c',path.join(directory,'test-function.S'),'-o',object]);
    run(zig,['objcopy','-O','binary',object,binary]);
    const bytes=fs.readFileSync(binary);
    if(!bytes.equals(fs.readFileSync(path.join(directory,'test-function.bin'))))throw Error('Native qualification binary differs; shipped image was not replaced.');
    const sources=[];
    function walk(folder){for(const entry of fs.readdirSync(folder,{withFileTypes:true})){
      if(entry.name.startsWith('.')||['runs','dumps','__pycache__'].includes(entry.name))continue;
      const file=path.join(folder,entry.name),name=path.relative(engine,file).split(path.sep).join('/');
      if(entry.isDirectory())walk(file);else if(entry.isFile()&&name!=='core-build.json'){
        const content=fs.readFileSync(file);if(name.endsWith('.cjs'))new vm.Script(content.toString(),{filename:name});
        sources.push([name,sha(content)]);
      }
    }}
    walk(engine);sources.sort(([a],[b])=>a.localeCompare(b));
    const report={format:'iz2s.core-build',version:1,name:'IZ2S Core',release:'0.21.0',compiler:'Zig 0.15.2',
      sourceSha256:sha(JSON.stringify(sources)),sourceFiles:sources.length,
      bootstrapSha256:sha(fs.readFileSync(path.join(engine,'upstream/bootstrap.bas'))),
      abi:{protocol:1,consolePort:39380,startArguments:5,saveName:'NS2IZM'},
      nativePayload:{bytes:payload.bytes,sha256:payload.sha256,recompiled:true,matched:true},
      nativeQualification:{bytes:bytes.length,sha256:sha(bytes),recompiled:true,matched:true},
      hostImplementation:['adapter.cjs','transport.cjs','runtime/contract.cjs','upstream/engine/lib/connection.cjs','upstream/engine/lib/pipeline-runner.cjs']};
    fs.writeFileSync(path.join(engine,'core-build.json'),JSON.stringify(report,null,2)+'\n');return report;
  } finally {fs.rmSync(temporary,{recursive:true,force:true});}
}
if(require.main===module){try{const args=process.argv.slice(2),options={};for(let i=0;i<args.length;i++){if(args[i]==='--zig')options.zig=args[++i];else if(args[i]==='--engine')options.engine=args[++i];else throw Error('Usage: node tools/build-iz2s-core.cjs [--zig PATH] [--engine PATH]');}const report=build(options);console.log(JSON.stringify(report,null,2));}catch(error){console.error(error.stderr?.toString()||error.message);process.exitCode=1;}}
module.exports={build};
