const path=require('node:path'),{execFile}=require('node:child_process');
async function focus(id,{run=execFile}={}){
 if(typeof id!=='string'||!/^\{?[a-fA-F0-9-]{36}\}?$/.test(id))throw Error('Invalid captured window identifier');
 return new Promise((resolve,reject)=>run('python3',[path.join(__dirname,'remote/window-frames.py'),'--focus',id],{timeout:2500,maxBuffer:16384},(error,out)=>{
  if(error)return reject(Error('Could not focus the captured window. Activate the game on the PC.'));
  try{const result=JSON.parse(out);if(result.focused!==true)throw Error(result.error||'Activate the game on the PC');resolve(result);}catch(e){reject(e);}
 }));
}
module.exports={focus};
