'use strict';
const test=require('node:test'),assert=require('node:assert/strict');
const wifi=require('./windows-wifi.cjs');
const one='00000000-0000-0000-0000-000000000001',two='00000000-0000-0000-0000-000000000002';
test('native Windows inventory selects connected physical Wi-Fi without changing adapters',async()=>{
 let calls=0;
 const result=await wifi.inspect('/tmp/test',{platform:'win32',run:async(program,args)=>{
  calls++;assert.equal(program,'powershell.exe');assert.equal(args.at(-1),'Inspect');
  return {stdout:'\ufeff'+JSON.stringify({adapters:[{id:one,name:'Беспроводная сеть',status:'Up',connected:false},{id:two,name:'无线网络',status:'Up',connected:true}]})};
 }});
 assert.equal(calls,1);assert.equal(result.selectedId,two);assert.equal(result.ldnSupported,false);
});
test('disabled Wi-Fi is never selected and non-Windows performs no PowerShell calls',async()=>{
 const run=async()=>({stdout:JSON.stringify({adapters:[{id:one,status:'Disabled'}]})});
 assert.equal((await wifi.inspect('/tmp',{platform:'win32',run})).selectedId,'');
 await wifi.inspect('/tmp',{platform:'linux',run:()=>{throw Error('Must not execute');}});
});
test('invalid GUIDs and shell arguments are refused',async()=>{
 await assert.rejects(wifi.inspect('/tmp',{platform:'win32',run:async()=>({stdout:JSON.stringify({adapters:[{id:'x; shutdown'}]})})}),/inventory/);
 assert.throws(()=>wifi.args('/tmp',';invalid'),/adapter/);
});
test('Windows discovery uses native type even for renamed Wi-Fi and excludes virtual networks',async()=>{
 const {candidates}=require('../../engines/CivSwitch/runtime/discovery.cjs');
 const row=address=>[{address,netmask:'255.255.255.252',family:'IPv4',internal:false}];
 const result=await candidates({platform:'win32',mode:'wifi',interfaces:()=>({'My adapter':row('192.168.3.1'),Ethernet:row('192.168.4.1'),VPN:row('10.0.0.1')}),wireless:async()=>false,tunnel:async()=>false,
 adapterModes:async()=>new Map([['192.168.3.1','wifi'],['192.168.4.1','lan'],['10.0.0.1','excluded']])});
 assert.deepEqual(result.targets,['192.168.3.2']);
});

function inventory(overrides={}){
 return {wlanService:'Running',administrator:false,capture:{installed:false},adapters:[
  {id:one,name:'Built-in',status:'Up',connected:true,wlan:{available:true,interfaceState:1,radioEnabled:true,phyTypes:[7,8]}},
  {id:two,name:'USB',status:'Disconnected',connected:false,wlan:{available:true,interfaceState:4,radioEnabled:true,phyTypes:[10]}}
 ],...overrides};
}
test('all physical adapters receive separate evidence; ordinary Wi-Fi never proves LDN',async()=>{
 const result=await wifi.inspect('/test',{platform:'win32',run:async()=>({stdout:JSON.stringify(inventory({ldnSupported:true}))})});
 assert.equal(result.adapters.length,2);assert.equal(result.adapters[1].compatibility.ordinaryWifi,true);
 assert.deepEqual(result.adapters[1].phyTypes,['802.11ax']);assert.equal(result.selectedId,one);
 assert.equal(result.ldnSupported,false);assert.equal(result.ldn.reason,'windows-ldn-transport-missing');
 for(const adapter of result.adapters){
  assert.equal(adapter.monitor.status,'unknown');assert.equal(adapter.compatibility.status,'unverified');
  for(const cap of ['rawReceive','rawTransmit','ldnAssociation','networkRestore'])assert.equal(adapter.compatibility[cap],'unverified');
 }
});
test('Npcap probe reads modes only; advertised monitor is not a radio transmission test',async()=>{
 const calls=[];
 const result=await wifi.inspect('/test',{platform:'win32',systemRoot:'C:\\Windows',run:async(program,args)=>{
  calls.push({program,args});
  if(program==='powershell.exe')return {stdout:JSON.stringify(inventory({administrator:true,capture:{installed:true,service:'Running',helperAvailable:true}}))};
  assert.equal(program,'C:\\Windows\\System32\\Npcap\\WlanHelper.exe');assert.equal(args.length,2);assert.equal(args[1],'modes');
  return {stdout:args[0]===one?'managed, monitor':'managed, master'};
 }});
 assert.equal(calls.length,3);assert.equal(result.adapters[0].monitor.status,'advertised');
 assert.equal(result.adapters[1].monitor.status,'unsupported');assert.equal(result.ldnSupported,false);
 assert.equal(result.adapters[0].compatibility.rawReceive,'unverified');
});
test('no UAC helper is run by automatic diagnostics without administrator rights',async()=>{
 let calls=0;
 const result=await wifi.inspect('/test',{platform:'win32',run:async()=>{
  calls++;return {stdout:JSON.stringify(inventory({capture:{installed:true,service:'Running',helperAvailable:true}}))};
 }});
 assert.equal(calls,1);assert.equal(result.adapters[0].monitor.reason,'administrator-required');
});
test('probe errors and unrecognized output stay unknown rather than marking hardware incompatible',async()=>{
 const result=await wifi.inspect('/test',{platform:'win32',run:async(program,args)=>{
  if(program==='powershell.exe')return {stdout:JSON.stringify(inventory({administrator:true,capture:{installed:true,service:'Running',helperAvailable:true}}))};
  if(args[0]===one)throw Error('Access denied');return {stdout:'Failure: monitor unavailable'};
 }});
 assert.deepEqual(result.adapters.map(a=>a.monitor.status),['unknown','unknown']);
 assert.deepEqual(result.adapters.map(a=>a.monitor.reason),['probe-failed','unexpected-probe-output']);
});
test('missing hardware, failed inventory, stopped service and radio off are distinct',()=>{
 assert.equal(wifi.assess(inventory({adapters:[]})).status,'no-adapter');
 assert.equal(wifi.assess(inventory({adapters:[],inventoryError:'CIM unavailable'})).status,'inspection-failed');
 const stopped=wifi.assess(inventory({wlanService:'Stopped'}));
 assert.equal(stopped.status,'service-unavailable');assert.equal(stopped.adapters[0].compatibility.ordinaryWifi,false);
 const input=inventory();input.adapters[0].wlan.radioEnabled=false;input.adapters[1].status='Disabled';
 const off=wifi.assess(input);assert.equal(off.selectedId,'');assert.equal(off.status,'adapter-unavailable');
 assert(off.adapters[0].compatibility.reasons.includes('radio-off'));
});
test('native Windows query and C# buffer parsing', {skip:process.platform!=='win32'},async()=>{
 const path=require('node:path'),{execFileSync}=require('node:child_process');
 const output=execFileSync('powershell.exe',['-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',path.join(__dirname,'windows-wlan.test.ps1')],{encoding:'utf8',windowsHide:true,timeout:30000});
 assert.match(output,/WLAN_BUFFER_TESTS_OK/);
 const report=await wifi.inspect(path.resolve(__dirname,'../..'));
 assert.equal(report.format,'iz2s.windows-wifi.v1');assert.equal(report.ldnSupported,false);
 assert(Array.isArray(report.adapters));assert.equal(report.inventoryError,'');
});
