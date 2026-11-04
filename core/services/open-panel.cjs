// The panel uses a normal browser window; module UIs belong to its workspace.
const fs=require('node:fs'),path=require('node:path'),{spawn}=require('node:child_process');
const {browserCommand}=require('../platform/desktop.cjs');
function openPanel(url,{env=process.env,launch=spawn,exists=fs.existsSync,main=false,...options}={}) {
  const u=new URL(url);
  if(u.protocol!=='http:'||u.hostname!=='127.0.0.1'||u.username||u.password)throw Error('Invalid local panel URL');
  if(env.HB_OPEN_PANEL==='0')return false;
  if(!main){
    const session=require('../session-workspace.cjs').current();if(session){session.open(u.href).catch(error=>console.error('Module tab: '+error.message));return true;}
    const root=env.IZ2S_ROOT||env.IZ15_ROOT||path.resolve(__dirname,'../..'),moduleId=env.IZ2S_MODULE_ID||env.IZ15_MODULE_ID;
    const id=moduleId?'module-'+moduleId:'interface-'+require('node:crypto').createHash('sha256').update(u.origin).digest('hex').slice(0,16);
    require('../workspace.cjs').register(root,{id,title:moduleId||'Module',url:u.href,moduleId,ownerPid:process.pid}).catch(error=>console.error('Module tab: '+error.message));return true;
  }
  if(u.pathname!=='/'||!/^#[a-f0-9]{48}$/.test(u.hash))throw Error('Invalid local panel URL');
  const [program,args]=browserCommand(u.href,{env,exists,...options});
  const child=launch(program,args,{env,detached:true,stdio:'ignore',shell:false});
  child.once('error',error=>console.error('Could not open IZ2S: '+error.message));child.unref();return true;
}
module.exports={openPanel,browserCommand};
