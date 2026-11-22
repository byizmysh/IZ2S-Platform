'use strict';
const path=require('node:path'),{execFile}=require('node:child_process'),{promisify}=require('node:util');
const execute=promisify(execFile);
const GUID=/^[a-f0-9]{8}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{12}$/i;
const PHY={1:'FHSS',2:'DSSS',3:'IR',4:'802.11a',5:'802.11b',6:'802.11g',7:'802.11n',8:'802.11ac',9:'802.11ad',10:'802.11ax',11:'802.11be'};
// A driver declaration is not an over-the-air test. Keep these evidence levels
// separate; neither Npcap nor monitor support supplies our missing LDN transport.
function assess(result){
 const adapters=result.adapters.map(adapter=>{
  const wlan=adapter.wlan||{},reasons=[];
  if(adapter.status==='Disabled')reasons.push('adapter-disabled');
  if(result.wlanService!=='Running')reasons.push('wlan-service-unavailable');
  if(wlan.available!==true)reasons.push('native-wlan-unavailable');
  if(wlan.radioEnabled===false)reasons.push('radio-off');
  const monitor=adapter.monitor||{status:'unknown',reason:'not-checked'};
  if(monitor.status==='unsupported')reasons.push('monitor-not-advertised');
  return {...adapter,phyTypes:(wlan.phyTypes||[]).map(id=>PHY[id]||'PHY '+id),monitor,
   compatibility:{status:'unverified',reasons,ordinaryWifi:wlan.available===true&&adapter.status!=='Disabled'&&result.wlanService==='Running'&&wlan.radioEnabled!==false,
    rawReceive:'unverified',rawTransmit:'unverified',ldnAssociation:'unverified',networkRestore:'unverified'}};
 });
 const enabled=adapters.filter(a=>a.status!=='Disabled'&&a.wlan?.radioEnabled!==false).sort((a,b)=>a.id.localeCompare(b.id));
 const selected=enabled.find(a=>a.connected)||enabled.find(a=>a.compatibility.ordinaryWifi)||enabled[0];
 return {format:'iz2s.windows-wifi.v1',checkedAt:new Date().toISOString(),...result,adapters,selectedId:selected?.id||'',
  status:result.inventoryError?'inspection-failed':!adapters.length?'no-adapter':!selected?'adapter-unavailable':result.wlanService!=='Running'?'service-unavailable':'ldn-unavailable',
  ldnSupported:false,ldn:{status:'unavailable',reason:'windows-ldn-transport-missing'}};
}
async function monitorProbe(adapter,result,{run,systemRoot}){
 const unknown=reason=>({status:'unknown',reason});
 if(adapter.status==='Disabled')return unknown('adapter-disabled');
 if(adapter.wlan?.operationMode===0x80000000)return {status:'active',source:'Native WLAN current operation mode'};
 if(!result.capture?.installed||!result.capture?.helperAvailable)return unknown('capture-tool-missing');
 if(result.capture.service!=='Running')return unknown('capture-service-unavailable');
 // WlanHelper can request UAC through NpcapHelper. Never prompt or change radio
 // mode during automatic checks; without elevation leave this fact unknown.
 if(!result.administrator)return unknown('administrator-required');
 try{
  const helper=path.win32.join(systemRoot,'System32','Npcap','WlanHelper.exe');
  const {stdout}=await run(helper,[adapter.id,'modes'],{windowsHide:true,timeout:3000,maxBuffer:65536});
  const output=stdout.trim();
  if(!/^(managed|monitor|master|wfd_device|wfd_owner|wfd_client)(\s*,\s*(managed|monitor|master|wfd_device|wfd_owner|wfd_client))*$/.test(output))return unknown('unexpected-probe-output');
  const modes=output.split(/\s*,\s*/);
  return {status:modes.includes('monitor')?'advertised':'unsupported',source:'Npcap WlanHelper modes',modes};
 }catch(error){return {...unknown('probe-failed'),error:String(error.message).slice(0,500)};}
}
async function inspect(root,{run=execute,platform=process.platform,systemRoot=process.env.SystemRoot||'C:\\Windows'}={}){
 if(platform!=='win32')return {adapters:[],recoveryAvailable:false,ldnSupported:false};
 const {stdout}=await run('powershell.exe',['-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',path.join(root,'core/platform/windows-wifi.ps1'),'-Action','Inspect'],{windowsHide:true,timeout:20000,maxBuffer:1024*1024});
 const result=JSON.parse(stdout.trim().replace(/^\uFEFF/,''));
 if(!Array.isArray(result.adapters)||result.adapters.length>64||result.adapters.some(a=>!a||!GUID.test(a.id)))throw Error('Invalid Windows Wi-Fi inventory');
 // Limit concurrent helper processes even on hosts with many radios.
 for(let offset=0;offset<result.adapters.length;offset+=4){
  await Promise.all(result.adapters.slice(offset,offset+4).map(async adapter=>{adapter.monitor=await monitorProbe(adapter,result,{run,systemRoot});}));
 }
 return assess(result);
}
function args(root,id,recover=false){
 if(!GUID.test(id))throw Error('Select a physical Windows Wi-Fi adapter.');
 return ['-NoProfile','-ExecutionPolicy','Bypass','-File',path.join(root,'core/platform/windows-wifi.ps1'),'-Action',recover?'Recover':'Restart','-InterfaceGuid',id];
}
module.exports={inspect,args,assess};
