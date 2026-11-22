'use strict';
const test=require('node:test'),assert=require('node:assert/strict'),{stop}=require('./stop-process.cjs');
const child=()=>({pid:12345,exitCode:null,signalCode:null,kill:()=>{throw Error('Do not kill only the Windows parent');}});
test('Windows stops only the owned PID tree and coalesces concurrent requests',async()=>{
 const owned=child();let calls=0,finish;
 const run=async(program,args,options)=>{calls++;assert.match(program,/System32\\taskkill.exe$/);assert.deepEqual(args,['/PID','12345','/T','/F']);assert.equal(options.windowsHide,true);await new Promise(resolve=>{finish=resolve;});};
 const first=stop(owned,{platform:'win32',run}),second=stop(owned,{platform:'win32',run});
 finish();assert.equal(await first,true);assert.equal(await second,true);assert.equal(calls,1);
});
test('exited children never cause a kill of a reused PID',async()=>{
 for(const owned of [{...child(),exitCode:0},{...child(),signalCode:'SIGINT'}])
  assert.equal(await stop(owned,{platform:'win32',run:()=>assert.fail('must not run')}),false);
});
test('a refused Windows stop is not reported as completed',async()=>{
 await assert.rejects(stop(child(),{platform:'win32',run:async()=>{throw Error('Access denied');}}),/Access denied/);
});
test('Linux retains its existing signal path',async()=>{
 let received;const owned={...child(),kill:signal=>{received=signal;return true;}};
 assert.equal(await stop(owned,{platform:'linux',run:()=>assert.fail('must not run')}),true);assert.equal(received,'SIGINT');
});
