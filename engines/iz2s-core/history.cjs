"use strict";
const fs=require('node:fs/promises'),path=require('node:path');
const paths=require('./runtime.cjs');
const valid=id=>typeof id==='string'&&/^[a-f0-9]{8}(?:-[a-f0-9]{4}){3}-[a-f0-9]{12}$/.test(id);
const pending=new Set(['starting','waiting','connected','typing','review','checking','values','checked','saving','save_confirmation','stopping']);
function create({out,state,readLog}){
 const directory=paths.journal(out),cache=new Map();
 async function read(id){
  if(!valid(id))return null;
  const file=path.join(directory,id+'.json');
  try{
   const stat=await fs.lstat(file);if(!stat.isFile()||stat.isSymbolicLink()||stat.size>1024*1024)return null;
   const version=[stat.ino,stat.size,stat.mtimeMs,stat.ctimeMs].join(':');
   if(cache.get(id)?.version===version)return cache.get(id).row;
   const row=JSON.parse(await fs.readFile(file,'utf8'));
   if(row.id!==id||!['launch','setup'].includes(row.kind)||!Number.isFinite(Date.parse(row.startedAt)))return null;
   cache.set(id,{version,row});return row;
  }catch(e){if(e.code==='ENOENT')return null;if(e instanceof SyntaxError)return cache.get(id)?.row||null;throw e;}
 }
 function present(row,current){
  const active=current.owned&&current.attempt?.id===row.id;
  const success=row.core?.complete===true&&row.core.result===0&&!!row.core.pid||row.kind==='setup'&&row.state==='saved';
  const status=success?(active?'running':'complete'):active?(row.state==='typing'?'typing':row.state==='stopping'?'stopping':'starting'):row.state==='stopped'?'stopped':row.state==='cancelled'?'cancelled':'failed';
  return {id:row.id,created:row.startedAt,completed:row.finishedAt||null,kind:row.kind==='setup'?'bootstrap':'launch',source:'iz2s-core',loaderId:'iz2s-core',status,active:!!active,
   error:row.error?paths.displayPath(row.error,out):!active&&!success&&pending.has(row.state)?'Session interrupted':null};
 }
 async function list(){
  const current=await state(),names=await fs.readdir(directory).catch(e=>{if(e.code==='ENOENT')return [];throw e;}),rows=[];
  for(const name of names){if(!name.endsWith('.json'))continue;const row=await read(name.slice(0,-5));if(row)rows.push(present(row,current));}
  for(const id of cache.keys())if(!names.includes(id+'.json'))cache.delete(id);
  return rows.sort((a,b)=>b.created.localeCompare(a.created));
 }
 async function log(id){
  const row=await read(id);if(!row)return null;
  const data=await readLog(out,id,128*1024);
  if(!data.logBytes)throw Object.assign(Error('IZ2S log not found'),{statusCode:404});
  let offset=0,index=0;const stamps=data.logTimings;
  const events=data.log.split('\n').filter((line,i,all)=>line||i<all.length-1).map(message=>{
   offset+=Buffer.byteLength(message+'\n');while(index+1<stamps.length&&stamps[index][0]<offset)index++;
   const at=stamps[index]?.[1];return {at:at?new Date(at).toISOString():row.startedAt,type:'output',source:'IZ2S core',message};
  });
  return {source:'iz2s-core',events,truncated:data.logTruncated};
 }
 async function remove(id){
  const row=await read(id);if(!row)return false;
  const current=await state();if(current.owned&&current.attempt?.id===id)throw Error('An active attempt cannot be deleted');
  // Only journal-owned files. Keep latest.json and run evidence for live/recovery consumers.
  for(const suffix of ['.log','.log.timings.jsonl','-input.json','.json'])await fs.rm(path.join(directory,id+suffix),{force:true});
  cache.delete(id);return true;
 }
 return {list,log,remove};
}
module.exports={create,valid};
