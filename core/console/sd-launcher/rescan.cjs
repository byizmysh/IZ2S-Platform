'use strict';
const fs=require('node:fs/promises'),path=require('node:path');

async function submit(directory,supervisor){
 const request=path.join(directory,'sd-launcher-rescan.request');
 const pending=request+'.pending';
 try{await fs.access(pending);}
 catch(error){
  if(error.code!=='ENOENT')throw error;
  // Claim this request before awaiting the console. A later refresh still
  // has the original path available and survives this acknowledgement.
  try{await fs.rename(request,pending);}
  catch(error){if(error.code==='ENOENT')return false;throw error;}
 }
 await supervisor.rescan();
 await fs.rm(pending,{force:true});
 return true;
}

module.exports={submit};
