'use strict';
// Telemetry for the guarded interactive hardware trial. Owns no console memory
// and never writes controls; stop this observer before closing the trial owner.
const fs=require('node:fs/promises'),path=require('node:path'),assert=require('node:assert/strict');
const pause=ms=>new Promise(r=>setTimeout(r,ms));
async function main(session){
 assert(/^launcher-[A-Za-z0-9]+$/.test(session||''));const directory=path.resolve(__dirname,'../../data/sessions',session),rpc=require(path.join(directory,'shared/rpc.cjs')),manifest=require('../../core/console/sd-launcher/check.cjs').verify(),L=manifest.layout;
 const report=JSON.parse(await fs.readFile(path.join(directory,'sd-launcher.json')));assert(report.session===rpc.a.session&&report.bootstrapSha256===manifest.sha256&&report.startedThread&&report.allocationsRetained&&!report.closed);
 let stopping=false,last=null,applications=[];const stop=()=>{stopping=true;};process.once('SIGINT',stop);process.once('SIGTERM',stop);
 while(!stopping){
  const link=await rpc.request({command:'status'});if(!link.ready){await pause(1500);continue;}
  const b=await rpc.read(report.base+0x1000,L.bytes);assert(b.readUInt32LE(L.magic)===0x44535a49&&b.readUInt32LE(L.version)===1,'Trial context changed; do not reuse its addresses');
  const state={};for(const name of ['state','error','poisoned','active','count','scan_generation','rejected','last_app_error'])state[name]=b.readUInt32LE(L[name]);for(const name of ['frames','transitions','read_bytes','native_launches'])state[name]=Number(b.readBigUInt64LE(L[name]));state.last_id=b.subarray(L.last_id,L.last_id+48).toString().split('\0')[0];
  const revision=b.readUInt32LE(L.catalog_revision),count=b.readUInt32LE(L.catalog_count);assert(count<=15);if(!(revision&1)&&revision===(await rpc.read(report.base+0x1000+L.catalog_revision,4)).readUInt32LE())applications=Array.from({length:count},(_,i)=>{const at=L.catalog+i*112;return {id:b.subarray(at,at+48).toString().split('\0')[0],name:b.subarray(at+48,at+112).toString().split('\0')[0],source:'sd'};});
  const now=Date.now(),runtime={session:rpc.a.session,stage:'SD applications',menuReady:state.state===2,updated:new Date(now).toISOString(),available:[],installed:applications,active:state.active?{id:state.last_id,source:'sd'}:null,sd:{...state,path:'SD:'+require('../../core/module-storage.cjs').BASE+'/',autonomous:true,manualRecovery:report.manualRecovery},...(last?{lastFrameBatch:{frames:state.frames-last.frames,interval:(now-last.at)/1000}}:{})};
  await require('../../core/session/ledger.cjs').saveJson(path.join(directory,'run11-runtime.json'),runtime);last={frames:state.frames,at:now};await pause(1500);
 }
 process.off('SIGINT',stop);process.off('SIGTERM',stop);
}
if(require.main===module)main(process.argv[2]).catch(e=>{console.error(e);process.exitCode=1;});module.exports={main};
