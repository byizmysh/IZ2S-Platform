'use strict';
const fs=require('node:fs/promises'),path=require('node:path');
const {redact}=require('./ledger.cjs');
async function mergeDiagnostics(root,row,events){
 const result=[...events];
 if(!row?.runDirectory)return result;
 const directory=path.resolve(row.runDirectory);
 if(path.dirname(directory)!==path.join(path.resolve(root),'data/sessions')||!/^launcher-[A-Za-z0-9]+$/.test(path.basename(directory)))return result;
 const file=path.join(directory,'fs-rpc/switch2/pipeline.json');
 let report;
 try{const stat=await fs.stat(file);if(stat.size>2*1024*1024)return result;report=JSON.parse(await fs.readFile(file,'utf8'));}catch{return result;}
 if(!report.error||!Number.isFinite(Date.parse(report.finished)))return result;
 const message=redact(String(report.error).split('\n')[0]).slice(0,1600);
 if(!result.some(event=>String(event.message||'').includes(message)))
  result.push({at:report.finished,type:'error',source:'fs-rpc-report',message});
 if(row.stopRequestedAt&&Date.parse(report.finished)>=Date.parse(row.stopRequestedAt)){
  const context='FS RPC diagnostics were recorded after the PC session was stopped.';
  if(!result.some(event=>event.message===context))result.push({at:report.finished,type:'warning',source:'panel',message:context});
 }
 return result.sort((a,b)=>String(a.at||'').localeCompare(String(b.at||'')));
}
module.exports={mergeDiagnostics};
