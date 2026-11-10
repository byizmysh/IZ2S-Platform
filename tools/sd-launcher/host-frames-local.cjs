'use strict';
// Local only: actual supervisor C, modeled SDK boundary, no console session.
const fs=require('node:fs/promises'),path=require('node:path'),assert=require('node:assert/strict'),crypto=require('node:crypto'),{execFileSync}=require('node:child_process');
const root=path.resolve(__dirname,'../..'),sha=b=>crypto.createHash('sha256').update(b).digest('hex');
async function main(){
 const out=path.join(root,'data/build-cache/host-frames-'+Date.now());await fs.mkdir(out,{recursive:true});const logs=[],env={...process.env,TMPDIR:out,ASAN_OPTIONS:'detect_leaks=0',UBSAN_OPTIONS:'halt_on_error=1'};
 const report={format:'iz2s.host-frames-local-v1',passed:false,at:new Date().toISOString(),outputDirectory:out,consoleCommands:0,installed:false,ordinaryLoaderActivated:false,hardwareQualified:false,enginesChanged:false,performanceGainPercent:null,scope:'Common C supervisor executes prepared app frame commands, fresh HID and menu return on the host. Vulkan/SD/SDK boundaries are modeled. Native engine workers are not stopped or freed by this component.',limitations:['The ordinary app loader still needs isolated GPU control allocations before activating delegation','PC service work holds its explicit display lease until known completion; loss during that boundary is not yet recoverable','No physical console/input/GPU qualification','Does not authorize freeing unknown native engine/DSP/FS resources'],sourceSha256:{}};
 function run(command,args){try{const output=execFileSync(command,args,{cwd:root,env,encoding:'utf8',timeout:60000,stdio:['ignore','pipe','pipe']});logs.push({command,args,output});return output;}catch(error){logs.push({command,args,failed:true,status:error.status,stdout:String(error.stdout||''),stderr:String(error.stderr||'')});throw error;}}
 const sources=['tests/sd-launcher/host-frames-model.c','core/console/sd-launcher/native/launcher.c','core/console/sd-launcher/native/native-loader.c','core/console/sd-launcher/native/filesystem.c','core/console/sd-launcher/native/memory.c','core/console/sd-launcher/native/library.c',...require('./model-sources.cjs').legacySources];
 try{
  report.nativeBuild=await require('../../core/console/sd-launcher/build.cjs').main();
  const P=require('../../core/console/sd-launcher/package.cjs'),shader=Buffer.alloc(20);shader.writeUInt32LE(0x07230203);
  function image(id){return P.encode({id,name:id,manifest:{},files:{state:{bytes:Buffer.alloc(1024)},data:{bytes:Buffer.alloc(4)},vertex:{bytes:shader},fragment:{bytes:shader},compute:{bytes:shader}}},{tile:Buffer.alloc(131072),pad:shader});}
  const one=path.join(out,'one.izapp'),two=path.join(out,'two.izapp'),binary=path.join(out,'model');await fs.writeFile(one,image('sd-proof-one'));await fs.writeFile(two,image('sd-proof-two'));
  run('cc',['-std=c11','-O1','-g','-fno-pie','-no-pie','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=address,undefined','-fno-builtin',...sources,'-o',binary]);report.supervisor=JSON.parse(run(binary,[one,two]));assert(report.supervisor.passed);
  report.regressions=run(process.execPath,['--test','tests/sd-launcher/host-frames.test.cjs','tests/sd-launcher/mixed-menu-host.test.cjs','tests/sd-launcher/mixed-menu.test.cjs','tests/sd-launcher/status.test.cjs','tests/sd-launcher/ordinary.test.cjs','tests/sd-launcher/checkpoint-close.test.cjs']);
  for(const file of [...sources,'core/console/sd-launcher/native/abi.h','core/console/sd-launcher/native/host-frames.h','core/console/sd-launcher/host-frames.cjs','core/console/sd-launcher/mixed-menu.cjs','core/console/sd-launcher/bootstrap.cjs','core/console/sd-launcher/build.cjs','tests/sd-launcher/host-frames.test.cjs','tools/sd-launcher/host-frames-local.cjs'])report.sourceSha256[file]=sha(await fs.readFile(path.join(root,file)));
  require('../../core/console/sd-launcher/check.cjs').verify();report.passed=true;
 }finally{
  await fs.writeFile(path.join(out,'commands.json'),JSON.stringify(logs,null,2)+'\n');await fs.writeFile(path.join(out,'report.json'),JSON.stringify(report,null,2)+'\n');await fs.writeFile(path.join(root,'data/diagnostics/host-frames-local.json'),JSON.stringify(report,null,2)+'\n');
 }
 console.log(JSON.stringify({passed:report.passed,supervisor:report.supervisor,ordinaryLoaderActivated:false,consoleCommands:0,report:path.join(root,'data/diagnostics/host-frames-local.json')}));return report;
}
module.exports={main};if(require.main===module)main().catch(error=>{console.error(error.stack);process.exitCode=1;});
