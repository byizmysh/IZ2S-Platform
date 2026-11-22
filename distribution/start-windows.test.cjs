'use strict';
const test=require('node:test'),assert=require('node:assert/strict');
const fs=require('node:fs/promises'),path=require('node:path'),os=require('node:os'),crypto=require('node:crypto');
const {start,ready,setupLock}=require('./start-windows.cjs');
async function fixture(t) {
 const root=await fs.mkdtemp(path.join(os.tmpdir(),'iz2s portable test '));
 t.after(()=>fs.rm(root,{recursive:true,force:true}));
 await fs.mkdir(path.join(root,'data/state'),{recursive:true});
 return root;
}
test('first Start prepares this folder, then launches the real panel',async t=>{
 const root=await fixture(t),calls=[];
 await start({root,platform:'win32',args:['--no-open'],isReady:async()=>false,
  install:async options=>{assert.equal(options.source,root);assert.equal(options.portable,true);assert.equal(options.launch,false);assert.equal(options.createShortcut,false);calls.push('install');},
  launch:async(r,options)=>{assert.equal(r,root);assert.deepEqual(options.args,['--no-open']);assert(options.node.endsWith('node.exe'));calls.push('launch');}});
 assert.deepEqual(calls,['install','launch']);
 await assert.rejects(fs.stat(path.join(root,'data/state/windows-setup.lock')),{code:'ENOENT'});
});
test('repeat Start uses the existing runtime without downloading or reinstalling',async t=>{
 const root=await fixture(t);let launches=0;
 await start({root,platform:'win32',args:[],isReady:async()=>true,
  install:async()=>assert.fail('unexpected installation'),launch:async()=>{launches++;}});
 assert.equal(launches,1);
});
test('setup failure releases the lock, records the error and never opens a panel',async t=>{
 const root=await fixture(t);
 await assert.rejects(start({root,platform:'win32',args:[],isReady:async()=>false,
  install:async()=>{throw Error('download unavailable');},launch:async()=>assert.fail('unexpected launch')}),/download unavailable/);
 await assert.rejects(fs.stat(path.join(root,'data/state/windows-setup.lock')),{code:'ENOENT'});
 assert.match(await fs.readFile(path.join(root,'data/state/setup.log'),'utf8'),/download unavailable/);
});
test('setup-only does not launch and concurrent starts wait for the owner',async t=>{
 const root=await fixture(t),release=await setupLock(root);let acquired=false;
 const pending=setupLock(root).then(unlock=>{acquired=true;return unlock;});
 await new Promise(resolve=>setTimeout(resolve,50));assert.equal(acquired,false);
 await release();await (await pending)();
 await start({root,platform:'win32',args:['--setup-only'],isReady:async()=>true,
  launch:async()=>assert.fail('unexpected launch')});
});
test('missing and relocated Python runtimes trigger repair',async t=>{
 const root=await fixture(t);
 await fs.writeFile(path.join(root,'package.json'),JSON.stringify({version:'0.3.0'}));
 assert.equal(await ready(root),false);
 await fs.writeFile(path.join(root,'data/state/installation.json'),JSON.stringify({status:'complete',version:'0.3.0',platform:'win32'}));
 await fs.mkdir(path.join(root,'data/runtime/node'),{recursive:true});
 await fs.writeFile(path.join(root,'data/runtime/node/node.exe'),'test');
 assert.equal(await ready(root,{execute:async()=>{throw Error('moved Python');}}),false);
 assert.equal(await ready(root,{execute:async()=>{}}),true);
});
test('portable setup retries a partial installation without replacing source or user data',async t=>{
 const root=await fixture(t),nodeHome=path.join(root,'bootstrap/node');
 await fs.mkdir(nodeHome,{recursive:true});await fs.writeFile(path.join(nodeHome,'node.exe'),'node fixture');
 await fs.mkdir(path.join(root,'distribution'),{recursive:true});
 const pkg=Buffer.from(JSON.stringify({version:'0.3.0'}));await fs.writeFile(path.join(root,'package.json'),pkg);
 await fs.writeFile(path.join(root,'distribution/files.json'),JSON.stringify({format:'iz2s.runtime-distribution',version:1,files:{'package.json':{bytes:pkg.length,sha256:crypto.createHash('sha256').update(pkg).digest('hex'),executable:false}}}));
 await fs.writeFile(path.join(root,'distribution/profile.json'),JSON.stringify({hostPlatforms:[]}));
 await fs.writeFile(path.join(root,'data/state/keep.txt'),'user data');
 const {install}=require('./install.cjs');let fail=true;const calls=[];
 const options={source:root,nodeHome,platform:'win32',arch:'x64',portable:true,uvProgram:'uv-fixture',createShortcut:false,launch:false,
  execute:async(program,args)=>{calls.push(args);if(fail&&args[0]==='venv'){fail=false;throw Error('interrupted setup');}},download:async()=>assert.fail('unexpected download')};
 await assert.rejects(install(options),/interrupted setup/);
 await install(options);
 assert.equal(await fs.readFile(path.join(root,'data/state/keep.txt'),'utf8'),'user data');
 assert.equal(JSON.parse(await fs.readFile(path.join(root,'data/state/installation.json'))).portable,true);
 assert(calls.some(args=>args[0]==='venv'&&args.includes('--clear')));
 await assert.rejects(install({...options,destination:root+'-other'}),/extracted folder/);
});
