const fs=require('node:fs/promises'),path=require('node:path'),{execFile}=require('node:child_process'),{promisify}=require('node:util');
const supported=()=>process.platform==='darwin'&&process.arch==='arm64';
function createInputLink(){return {adapter:'mac2jb-native',inspect:async()=>({reachable:supported(),moduleReady:false,keyboard:false,error:'Use the native mac2jb app to verify USB and manage its own session.'}),performPlan:async()=>{throw Error('mac2jb manages its own bootstrap; IZ2S typed plans are not forwarded')},halt:async()=>({stopped:true})};}
async function createChecks({configuration}){return {inspect:async()=>({at:new Date().toISOString(),ready:false,items:[{id:'server',label:'Mac · server + input',ready:supported(),detail:supported()?'Apple Silicon Mac · native companion workflow':'Requires Apple Silicon Mac with macOS 14 or newer'},{id:'usb',label:'USB → Switch',ready:false,detail:'Verify the approved USB driver and enumeration in mac2jb'},{id:'lan',label:'Console',ready:false,detail:'mac2jb owns the original console session; IZ2S integration is untested'}]})};}
async function open({configuration,execute=promisify(execFile),platform=process.platform,arch=process.arch}){
 if(platform!=='darwin'||arch!=='arm64')throw Error('Open this configuration on an Apple Silicon Mac');
 const app=configuration.settings.mac2jbApp;
 if(!path.isAbsolute(app)||!app.endsWith('.app')||/[\0\r\n]/.test(app))throw Error('Choose an absolute mac2jb .app path');
 if(!await fs.stat(app).then(s=>s.isDirectory(),()=>false))throw Error('Install the original mac2jb app first');
 await execute('/usr/bin/open',['-a',app]);return {ok:true,external:true};
}
module.exports={createInputLink,createChecks,open};
