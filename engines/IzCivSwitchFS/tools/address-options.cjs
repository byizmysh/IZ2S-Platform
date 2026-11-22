'use strict';
const {execFile}=require('node:child_process');
const {promisify}=require('node:util');
const run=promisify(execFile);
const {privateIP}=require('../../../core/session/network-discovery.cjs');
const discovery=require('../../CivSwitch/runtime/discovery.cjs');

// Read the OS neighbour cache; do not manufacture console candidates from every
// unused address in a subnet. Only the loader protocol confirms a console.
async function neighbours({platform=process.platform,execute=run}={}) {
 try {
  if(platform==='linux'){
   const {stdout}=await execute('ip',['-j','-4','neigh','show'],{timeout:2000,maxBuffer:256*1024});
   return JSON.parse(stdout).filter(row=>row.lladdr&&!row.state?.some?.(state=>['FAILED','INCOMPLETE'].includes(state))).map(row=>row.dst);
  }
  const {stdout}=await execute('arp',['-a'],{timeout:2000,maxBuffer:256*1024,windowsHide:true});
  return stdout.match(/\b(?:\d{1,3}\.){3}\d{1,3}\b/g)||[];
 } catch { return []; }
}
async function list({known=[],found=[],readNeighbours=neighbours,attached=discovery.candidates}={}){
 const hints=[...new Set([...found,...known,...await readNeighbours()])].filter(privateIP);
 const {preferred}=await attached({known:hints});
 return [...new Set(preferred)].map(address=>({address,verified:found.includes(address)}))
  .sort((a,b)=>Number(b.verified)-Number(a.verified)||a.address.localeCompare(b.address,undefined,{numeric:true}));
}
module.exports={list,neighbours};
