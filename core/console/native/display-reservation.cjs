'use strict';
// Display ownership is durable control state, separate from app-runtime.json
// telemetry, which the menu can publish while a requester is being admitted.
const fs=require('node:fs/promises'),path=require('node:path');
const filename=directory=>path.join(directory,'native-display-claim.lock','owner.json');
async function write(directory,request,id){
 await require('../../session/ledger.cjs').saveJson(filename(directory),{version:1,session:request.session,token:request.token,ownerPid:request.ownerPid,id});
}
async function matches(directory,request,runtime){
 let owner;try{owner=JSON.parse(await fs.readFile(filename(directory)));}catch(error){if(error.code!=='ENOENT')throw error;}
 if(owner)return owner.version===1&&owner.session===request.session&&owner.token===request.token&&owner.ownerPid===request.ownerPid;
 // Older diagnostic clients reserve via telemetry. A new client must retain
 // its separate record; losing that record never falls back to telemetry.
 return request.reservation!==1&&runtime?.active?.ownerPid===request.ownerPid;
}
async function remove(directory){await fs.rm(filename(directory),{force:true});}
module.exports={write,matches,remove};
