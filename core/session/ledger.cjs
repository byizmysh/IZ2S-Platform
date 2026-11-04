const fs=require('node:fs/promises'),path=require('node:path'),crypto=require('node:crypto');
const logs=require('../services/rotating-log.cjs');
const json=require('../services/json-write.cjs');
const readJsonLinesTail=(file,options={})=>logs.readJson(file,{limit:250,maxBytes:1024*1024,...options});
const valid=/^[a-f0-9-]{36}$/;
const redact=value=>String(value).replace(/\x1b\[[0-?]*[ -/]*[@-~]/g,'').replace(/Orange\s*Pi/gi,'Input bridge').replace(/V=START\([^)]*\)/g,'V=START(…)').replace(/(https?:\/\/[^\s#]+\/#)[a-f0-9]{24,64}/gi,'$1[private]').replace(/(Bearer\s+)[^\s"']+/gi,'$1[private]').replace(/((?:token|secret|password|api[_-]?key|key)["']?\s*[:=]\s*["']?)[^\s"',;&]+/gi,'$1[private]');
async function readJson(file,fallback=null){try{return JSON.parse(await fs.readFile(file,'utf8'));}catch(e){if(e.code==='ENOENT')return fallback;throw e;}}
async function saveJson(file,value){return json.write(file,value,{directoryMode:0o700});}
function createLedger(root,{logOptions={}}={}){
 const policy=logs.policy({directoryMode:0o700,...logOptions}),readOptions={segmentBytes:policy.maxBytes,archives:policy.archives};
 const folder=path.join(root,'data/state/attempts');
 const location=id=>{if(!valid.test(id))throw Error('Invalid attempt identifier');return path.join(folder,id);};
 async function append(id,type,message,details={}){const dir=location(id);await logs.append(path.join(dir,'events.jsonl'),JSON.stringify({at:new Date().toISOString(),...details,type,message:redact(message).slice(0,1600)})+'\n',policy);}
 const ordered=rows=>rows.sort((a,b)=>String(a?.at||'').localeCompare(String(b?.at||'')));
 const recent=async id=>ordered(await logs.readJson(path.join(location(id),'events.jsonl'),{...readOptions,limit:250,maxBytes:1024*1024}));
 async function all(id){const rows=ordered(await logs.readJson(path.join(location(id),'events.jsonl'),{...readOptions,limit:logs.MAX_ENTRIES-1}));if(rows.retention?.truncated||rows.retention?.rotated){const first=rows[0]?.at,at=typeof first==='string'&&first.length<=64?first:new Date().toISOString();rows.unshift({at,type:'log-retention',message:'History may be incomplete: older events are limited by log rotation or retained export limits.'});}return rows;}
 async function listFailures(){await fs.mkdir(folder,{recursive:true});const out=[];for(const name of await fs.readdir(folder)){if(!valid.test(name))continue;const row=await readJson(path.join(folder,name,'attempt.json'));if(row&&['failed','cancelled'].includes(row.status))out.push(row);}return out.sort((a,b)=>b.created.localeCompare(a.created));}
 async function removeFailure(id,activeId){if(id===activeId)throw Error('An active attempt cannot be deleted');const dir=location(id),row=await readJson(path.join(dir,'attempt.json'));if(!row||!['failed','cancelled'].includes(row.status))throw Error('Only failed attempt logs can be deleted');if(row.runDirectory){const run=path.resolve(row.runDirectory);if(path.dirname(run)!==path.join(root,'data/sessions')||!/^launcher-[A-Za-z0-9]+$/.test(path.basename(run)))throw Error('Invalid run log directory');await fs.rm(run,{recursive:true,force:true});}await fs.rm(dir,{recursive:true,force:true});}
 return {append,recent,all,listFailures,removeFailure,read:id=>readJson(path.join(location(id),'attempt.json')),write:row=>saveJson(path.join(location(row.id),'attempt.json'),row),session:id=>path.join(location(id),'session')};
}
module.exports={createLedger,readJson,saveJson,redact,readJsonLinesTail};
