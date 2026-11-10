'use strict';
// Persistent host half of the future console-owned recovery channel. This is
// not a network listener and does not turn a protocol-1 session into protocol 3.
const fs=require('node:fs/promises'),path=require('node:path'),assert=require('node:assert/strict'),crypto=require('node:crypto');
const REQUEST_BYTES=0x8080,RESPONSE_BYTES=16+0x8000,ZERO='0'.repeat(64);
const digest=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
function validate(row,session){
 assert(row?.version===3&&row.session===session,'Recovery journal belongs to a different session');
 const c=row.committed;assert(c&&Number.isSafeInteger(c.sequence)&&c.sequence>=0&&/^[a-f0-9]{64}$/.test(c.hash));
 if(!c.sequence)assert(c.hash===ZERO&&!c.response);else{assert(typeof c.response==='string');const bytes=Buffer.from(c.response,'base64');assert(bytes.length===RESPONSE_BYTES&&bytes.toString('base64')===c.response);}
 if(row.pending){const p=row.pending;assert(Number.isSafeInteger(p.sequence)&&p.sequence===c.sequence+1&&/^[a-f0-9]{64}$/.test(p.hash));const bytes=Buffer.from(p.packet,'base64');assert(bytes.length===REQUEST_BYTES&&bytes.toString('base64')===p.packet&&digest(bytes)===p.hash,'Recovery pending packet changed');}
}
async function open({directory,session,save=require('../../session/ledger.cjs').saveJson}){
 assert(/^[a-f0-9]{32}$/.test(session));const file=path.join(directory,'native-recovery-journal.json');let row;
 try{row=JSON.parse(await fs.readFile(file,'utf8'));}catch(e){if(e.code!=='ENOENT')throw e;row={version:3,session,committed:{sequence:0,hash:ZERO},pending:null};await save(file,row);}
 validate(row,session);let queue=Promise.resolve(),uncertain=false,admitted=false;
 function serial(fn){const task=queue.then(()=>{assert(!uncertain,'Recovery journal persistence is uncertain; commands are fenced');return fn();});queue=task.catch(()=>{});return task;}
 async function publish(next){try{await save(file,next);}catch(e){uncertain=true;throw e;}row=next;}
 return {
  snapshot(){return structuredClone(row);},
  suspend(){return serial(()=>{admitted=false;});},
  prepare(packet){return serial(async()=>{assert(admitted,'Authenticate and reconcile the console before issuing commands');assert(Buffer.isBuffer(packet)&&packet.length===REQUEST_BYTES);assert(!row.pending,'Reconcile the previous command before issuing another');assert(row.committed.sequence<Number.MAX_SAFE_INTEGER,'Recovery sequence is exhausted');admitted=false;const pending={sequence:row.committed.sequence+1,hash:digest(packet),packet:packet.toString('base64')};await publish({...row,pending});return structuredClone(pending);});},
  reconcile(receipt){return serial(async()=>{
   admitted=false;
   assert(receipt?.session===session,'The console process has changed; old addresses cannot be resumed');
   assert(Number.isSafeInteger(receipt.sequence)&&receipt.sequence>=0&&/^[a-f0-9]{64}$/.test(receipt.hash)&&['idle','executing','completed'].includes(receipt.state),'Invalid authenticated console receipt');
   assert(receipt.sequence?receipt.state!=='idle':receipt.state==='idle'&&receipt.hash===ZERO,'Invalid console receipt state');
   const p=row.pending,c=row.committed;
   if(p&&receipt.sequence===p.sequence){assert(receipt.hash===p.hash,'A different command occupies the pending sequence');return {action:receipt.state==='completed'?'recover-cached-response':'wait-for-completion',sequence:p.sequence,hash:p.hash};}
   assert(receipt.sequence===c.sequence&&receipt.hash===c.hash&&receipt.state!=='executing','Console receipt differs from the committed session; commands remain fenced');
   if(p)return {action:'send-proven-unexecuted-request',...structuredClone(p)};admitted=true;return {action:'ready',sequence:c.sequence};
  });},
  commit({sequence,hash,response}){return serial(async()=>{const p=row.pending;assert(p&&p.sequence===sequence&&p.hash===hash,'Response differs from the pending command');assert(Buffer.isBuffer(response)&&response.length===RESPONSE_BYTES);await publish({...row,committed:{sequence,hash,response:response.toString('base64')},pending:null});admitted=true;return Buffer.from(response);});},
  get uncertain(){return uncertain;},file,
 };
}
module.exports={open,REQUEST_BYTES,RESPONSE_BYTES,ZERO,digest};
