'use strict';
// Observe cross-process atomic job saves without parsing every completed job on
// each input poll. The periodic full scan also covers legacy/in-place writers.
const fs=require('node:fs/promises'),path=require('node:path');
function create(root,{clock=Date.now,maximumAgeMs=1000,read=require('./jobs.cjs').all,stat=fs.stat}={}){
 let stamp=null,at=-Infinity,rows=[],pending;
 async function scan(force){
  let current;try{const s=await stat(path.join(root,'jobs'),{bigint:true});current=[s.dev,s.ino,s.size,s.mtimeNs,s.ctimeNs].join(':');}catch(error){if(error.code!=='ENOENT')throw error;current='missing';}
  const now=clock();if(force||current!==stamp||now-at>=maximumAgeMs){rows=await read(root);stamp=current;at=now;}return rows;
 }
 return {async all({force=false}={}){if(pending){await pending;if(!force)return rows;}const work=scan(force);pending=work;try{return await work;}finally{if(pending===work)pending=null;}},invalidate(){at=-Infinity;}};
}
module.exports={create};
