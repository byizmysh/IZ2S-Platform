const fs=require('node:fs/promises'),path=require('node:path'),crypto=require('node:crypto');
const logs=require('../services/rotating-log.cjs');
const json=require('../services/json-write.cjs');
async function read(file,fallback=null){try{return JSON.parse(await fs.readFile(file,'utf8'));}catch(e){if(e.code==='ENOENT')return fallback;throw e;}}
async function write(file,value){return json.write(file,value);}
function clean(value){return String(value).replace(/V=START\([^)]*\)/g,'V=START(…)').replace(/(https?:\/\/[^\s#]+\/#)[a-f0-9]{24,64}/gi,'$1[private]').replace(/((?:token|secret|key)\s*[:=]\s*)[a-f0-9]{24,64}/gi,'$1[private]');}
async function event(root,type,message,extra={},logOptions={}){const row={id:crypto.randomUUID(),at:new Date().toISOString(),type,message:clean(message).slice(0,1200),...extra};await logs.append(path.join(root,'events.jsonl'),JSON.stringify(row)+'\n',logOptions);return row;}
module.exports={read,write,event,clean};
