// Host journals retain the current segment and two older segments. Rotation is
// per file, on append only; it never removes session directories or idle logs.
const fs=require('node:fs/promises'),path=require('node:path'),crypto=require('node:crypto');
const {readJsonLinesTail}=require('../session/jsonl-tail.cjs');
const json=require('./json-write.cjs');
const MAX_BYTES=8*1024*1024,ARCHIVES=2,MAX_ENTRIES=100000,MAX_EXPORT_BYTES=24*1024*1024;
const pending=new Map();
function policy({maxBytes=MAX_BYTES,archives=ARCHIVES,mode=0o600,directoryMode}={}){
 if(!Number.isInteger(maxBytes)||maxBytes<1||!Number.isInteger(archives)||archives<0||archives>8||!Number.isInteger(mode))throw new RangeError('Invalid log retention settings');
 return {maxBytes,archives,mode,...(directoryMode===undefined?{}:{directoryMode})};
}
function serialize(file,operation){
 const key=path.resolve(file),previous=pending.get(key)||Promise.resolve();
 const task=previous.catch(()=>{}).then(operation);pending.set(key,task);
 task.finally(()=>{if(pending.get(key)===task)pending.delete(key);}).catch(()=>{});
 return task;
}
async function stat(file){try{return await fs.stat(file);}catch(error){if(error.code==='ENOENT')return null;throw error;}}
async function tail(file,bytes){
 let handle,primary;
 try{
  handle=await fs.open(file,'r');const {size}=await handle.stat(),start=Math.max(0,size-bytes),buffer=Buffer.alloc(size-start);
  let filled=0;while(filled<buffer.length){const {bytesRead}=await handle.read(buffer,filled,buffer.length-filled,start+filled);if(!bytesRead)break;filled+=bytesRead;}
  return {bytes:buffer.subarray(0,filled),partial:start>0};
 }catch(error){primary=error;throw error;}finally{try{await handle?.close();}catch(error){if(!primary)throw error;}}
}
async function readRows(file,limit,maxBytes){
 if(limit<=250)return readJsonLinesTail(file,{limit,maxBytes});
 let data;try{data=await tail(file,maxBytes);}catch(error){if(error.code==='ENOENT')return [];throw error;}
 let bytes=data.bytes;if(data.partial){const newline=bytes.indexOf(10);bytes=newline<0?Buffer.alloc(0):bytes.subarray(newline+1);}
 const text=bytes.toString('utf8'),rows=[];let end=text.length;
 while(end>0&&rows.length<limit){
  const previous=text.lastIndexOf('\n',end-1),line=text.slice(previous+1,end);
  if(line.trim())try{rows.push(JSON.parse(line));}catch{}
  end=previous<0?0:previous;
 }
 return rows.reverse();
}
async function retention(file){
 try{const data=await tail(file+'.retention.json',2048);if(data.partial)return {historyOmitted:true,uncertain:true};const row=JSON.parse(data.bytes.toString('utf8'));if(row?.version!==1||row.historyOmitted!==true)return {historyOmitted:true,uncertain:true};return {historyOmitted:true};}
 catch(error){if(error.code==='ENOENT')return {historyOmitted:false};return {historyOmitted:true,uncertain:true};}
}
async function markLoss(file){
 const row=await retention(file);if(row.historyOmitted&&!row.uncertain)return;
 // Record possible loss before destructive rotation. If rotation later fails,
 // the conservative marker remains; the strict append still rejects normally.
 await json.write(file+'.retention.json',{version:1,historyOmitted:true,firstOmissionAt:new Date().toISOString()});
}
async function boundedArchive(file,target,maxBytes,mode){
 const data=await tail(file,maxBytes);let bytes=data.bytes;
 if(data.partial){const newline=bytes.indexOf(10);bytes=newline<0?Buffer.alloc(0):bytes.subarray(newline+1);}
 const last=bytes.lastIndexOf(10);bytes=last<0?Buffer.alloc(0):bytes.subarray(0,last+1);
 const temporary=target+'.'+crypto.randomUUID()+'.tmp';
 try{await fs.writeFile(temporary,bytes,{mode});await fs.rename(temporary,target);await fs.rm(file,{force:true});}
 catch(error){await fs.rm(temporary,{force:true}).catch(()=>{});throw error;}
}
async function rotate(file,size,{maxBytes,archives,mode}){
 const oldest=archives?await stat(file+'.'+archives):null;
 if((!archives&&size>0)||size>maxBytes||oldest?.size)await markLoss(file);
 if(!archives){await fs.rm(file,{force:true});return;}
 await fs.rm(file+'.'+archives,{force:true});
 for(let i=archives-1;i>=1;i--)try{await fs.rename(file+'.'+i,file+'.'+(i+1));}catch(error){if(error.code!=='ENOENT')throw error;}
 if(size>maxBytes)await boundedArchive(file,file+'.1',maxBytes,mode);
 else try{await fs.rename(file,file+'.1');}catch(error){if(error.code!=='ENOENT')throw error;}
}
function append(file,text,options={}){
 const settings=policy(options),bytes=Buffer.isBuffer(text)?Buffer.from(text):Buffer.from(String(text));
 if(bytes.length>settings.maxBytes)return Promise.reject(Object.assign(new RangeError('Log record exceeds segment capacity'),{code:'ERR_LOG_RECORD_TOO_LARGE'}));
 return serialize(file,async()=>{
  await fs.mkdir(path.dirname(file),{recursive:true,...(settings.directoryMode===undefined?{}:{mode:settings.directoryMode})});const current=await stat(file);let separator=Buffer.alloc(0);
  if(current){if(!current.isFile())throw Object.assign(Error('Log target is not a regular file'),{code:'EISDIR'});await fs.chmod(file,settings.mode);if(current.size){const last=await tail(file,1);if(last.bytes[0]!==10)separator=Buffer.from('\n');}}
  if(current&&current.size+separator.length+bytes.length>settings.maxBytes){await rotate(file,current.size,settings);separator=Buffer.alloc(0);}
  await fs.appendFile(file,separator.length?Buffer.concat([separator,bytes]):bytes,{mode:settings.mode});
 });
}
async function readJson(file,{limit=MAX_ENTRIES,segmentBytes=MAX_BYTES,archives=ARCHIVES,maxBytes=Math.min(segmentBytes*(archives+1),MAX_EXPORT_BYTES)}={}){
 policy({maxBytes:segmentBytes,archives});
 if(!Number.isInteger(limit)||limit<1||limit>MAX_ENTRIES||!Number.isInteger(maxBytes)||maxBytes<1)throw new RangeError('Invalid retained log read limits');
 // Reads never wait behind a stalled appender. Each opened fd is a bounded
 // snapshot; concurrent rotation can omit a segment, so UUIDs are deduplicated.
 let remaining=Math.min(maxBytes,MAX_EXPORT_BYTES-1024),entries=[],truncated=false;const seen=new Set();
 for(let i=0;i<=archives;i++){
  const segment=i?file+'.'+i:file,info=await stat(segment);if(!info?.size)continue;
  if(remaining<=0||entries.length>=limit){truncated=true;break;}
  const budget=Math.min(info.size,segmentBytes,remaining);remaining-=budget;if(info.size>budget)truncated=true;
  const rows=await readRows(segment,limit-entries.length,budget);
  if(rows.length>=limit-entries.length)truncated=true;
  const unique=rows.filter(row=>{if(!row||typeof row!=='object'||Array.isArray(row))return false;if(typeof row.id!=='string')return true;if(seen.has(row.id))return false;seen.add(row.id);return true;});
  entries=unique.concat(entries);
 }
 const bounded=[],outputLimit=Math.min(maxBytes,MAX_EXPORT_BYTES-1024);let outputBytes=2;
 for(let i=entries.length-1;i>=0&&bounded.length<limit;i--){const bytes=Buffer.byteLength(JSON.stringify(entries[i]))+1;if(outputBytes+bytes>outputLimit){truncated=true;break;}bounded.push(entries[i]);outputBytes+=bytes;}
 const state=await retention(file);
 entries=bounded.reverse();Object.defineProperty(entries,'retention',{value:{truncated,rotated:state.historyOmitted,maxBytes:outputLimit,limit}});
 return entries;
}
module.exports={append,readJson,policy,MAX_BYTES,ARCHIVES,MAX_ENTRIES,MAX_EXPORT_BYTES};
