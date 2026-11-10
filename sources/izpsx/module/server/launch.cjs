'use strict';
const fs=require('node:fs/promises'),path=require('node:path'),assert=require('node:assert/strict'),{spawn}=require('node:child_process'),{once}=require('node:events');
async function create({directory,app,spawnProcess=spawn}){
 assert(app.id==='izpsx'&&/^[a-f0-9]{64}$/.test(app.fingerprint));assert(/^launcher-[A-Za-z0-9]+$/.test(path.basename(directory)));
 let requested=false,closed=false,closing;
 return {async pending(){return !closed&&!requested;},async afterBatch(){if(closed)return;requested=true;return {returnToMenu:true};},close(){
  if(!closing)closing=(async()=>{closed=true;if(!requested)return;
   const output=await fs.open(path.join(directory,'izpsx-entry.log'),'a');try{
    const child=spawnProcess(process.execPath,[path.join(__dirname,'run-native.cjs'),directory,app.root,app.fingerprint],{cwd:path.resolve(directory,'../../..'),stdio:['ignore',output.fd,output.fd],env:{...process.env}});await once(child,'spawn');
   }finally{await output.close();}
  })();return closing;
 }};
}
module.exports={create};
