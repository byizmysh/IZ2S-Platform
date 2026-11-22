'use strict';
// Native Windows host checks. Input is simulated; the real server binds only
// loopback and never receives a console handshake or sends console commands.
const test=require('node:test'),assert=require('node:assert/strict');
const fs=require('node:fs/promises'),path=require('node:path'),os=require('node:os'),net=require('node:net');
const {spawn,execFile}=require('node:child_process'),{promisify}=require('node:util');
const execute=promisify(execFile),root=path.resolve(__dirname,'..');
const native={skip:process.platform!=='win32',timeout:60000};
function environment(){
 process.env.IZ2S_ROOT=root;
 require('../core/platform/runtime.cjs').configure(root);
}
async function until(check,description,timeout=15000){
 const deadline=Date.now()+timeout;
 while(Date.now()<deadline){if(await check())return;await new Promise(resolve=>setTimeout(resolve,100));}
 throw Error('Timed out: '+description);
}
async function portFree(){
 await new Promise((resolve,reject)=>{const server=net.createServer();server.once('error',reject);server.listen(39380,'127.0.0.1',()=>server.close(resolve));});
}
test('Windows SB4 has its private dependencies, working fonts and verified bootstrap',native,async t=>{
 environment();
 assert.equal(await require('./start-windows.cjs').ready(root),true);
 const rows=require('../hardware/dependencies.cjs').inspect({hostOnly:true,ssh:true});
 assert.deepEqual(rows.filter(row=>!row.ready),[]);
 assert((await require('../engines/iz2s-core/runtime.cjs').verify()).files>0);
 const plan=await require('../core/session/source-check.cjs').readPlan(root);
 assert.equal(plan.lines,229);assert.equal(plan.parts.length,3);
 const temporary=await fs.mkdtemp(path.join(os.tmpdir(),'IZ2S SB4 fonts '));
 t.after(()=>fs.rm(temporary,{recursive:true,force:true}));
 const image=path.join(temporary,'sb4.png');
 const script='import sys;sys.path.insert(0,sys.argv[1]);from PIL import Image,ImageDraw,ImageFont;from language.fonts import font;im=Image.new("RGB",(420,80));d=ImageDraw.Draw(im);f=ImageFont.truetype(font(),22);d.text((8,8),"SmileBASIC 4 — Windows",font=f,fill="white");im.save(sys.argv[2]);assert Image.open(sys.argv[2]).size==(420,80)';
 await execute(require('../core/platform/python.cjs').command(root),['-c',script,root,image],{windowsHide:true,timeout:20000});
 assert((await fs.stat(image)).size>200);
});
test('Windows SB4 starts its real listener, stops its process tree, and can start again',native,async t=>{
 environment();await portFree();
 const temporary=await fs.mkdtemp(path.join(os.tmpdir(),'IZ2S SB4 lifecycle '));
 await fs.mkdir(path.join(temporary,'data/state'),{recursive:true});
 await fs.writeFile(path.join(temporary,'data/state/launch-configurations.json'),JSON.stringify({selected:'pc-switch1',profiles:{'pc-switch1':{}}}));
 // Only the controller's local-address validation is substituted to exercise
 // production startup safely on loopback, regardless of the VM's real networks.
 t.mock.method(os,'networkInterfaces',()=>({test:[{address:'127.0.0.1',family:'IPv4',internal:false}]}));
 let child,controller;const plans=[];
 t.after(async()=>{
  if(child&&child.exitCode===null&&child.signalCode===null)await require('../core/platform/stop-process.cjs').stop(child);
  if(controller)await until(async()=>!(await controller.state()).busy,'test child shutdown');
  await fs.rm(temporary,{recursive:true,force:true});
 });
 controller=require('../engines/iz2s-core/controller.cjs').create({root:temporary,out:path.join(temporary,'core'),
  manager:{preview:false,snapshot:async()=>({active:null,connected:false,settings:{consoleHost:'127.0.0.1'},connections:{devices:{server:{paths:[{address:'127.0.0.1'}]}}}})},
  inputFactory:()=>({inspect:async()=>({keyboard:true,keyboardBusy:false}),performPlan:async plan=>{plans.push(plan);},halt:async()=>{}}),
  launch:(...args)=>{child=spawn(...args);return child;}});
 for(let attempt=0;attempt<2;attempt++){
  await controller.start({freshDirect:true});
  await until(async()=>{const state=await controller.state();if(state.attempt.error)throw Error(state.attempt.error);return state.attempt.state==='waiting';},'SB4 listener ready');
  const socket=net.createConnection({host:'127.0.0.1',port:39380});
  await new Promise((resolve,reject)=>{socket.once('connect',resolve);socket.once('error',reject);});
  socket.destroy();
  await controller.stop();
  await until(async()=>!(await controller.state()).busy,'all SB4 child processes closed');
  const state=await controller.state();assert.equal(state.attempt.state,'stopped');assert.equal(state.attempt.core,null);
  await portFree();
 }
 assert.equal(plans.length,2);
 assert(plans.every(plan=>plan.steps.some(step=>step.text==='V=START(39380,127,0,0,1)')));
});
