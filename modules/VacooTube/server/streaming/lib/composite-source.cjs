// One persistent named surface, with whole BMP frames delivered to replaceable
// FFmpeg scalers. Pausing/reconfiguring the stream does not release the surface.
const path=require('node:path'),{spawn}=require('node:child_process');
function scalerArgs(args,profile){const at=args.indexOf('-i');if(at<0)throw Error('Invalid scaler arguments');return ['-hide_banner','-loglevel','error','-nostdin','-filter_threads','1','-f','image2pipe','-vcodec','bmp','-framerate',String(profile.fps),'-i','pipe:0',...args.slice(at+2)];}
function bmpFrames(emit){let header=Buffer.alloc(14),head=0,frame=null,at=0;const free=[];return chunk=>{let offset=0;while(offset<chunk.length){if(!frame){const count=Math.min(14-head,chunk.length-offset);chunk.copy(header,head,offset,offset+count);head+=count;offset+=count;if(head<14)continue;const bytes=header.readUInt32LE(2);if(header.toString('ascii',0,2)!=='BM'||bytes<54||bytes>128*1024*1024)throw Error('Invalid offscreen frame');frame=free.find(b=>b.length===bytes)||Buffer.allocUnsafe(bytes);const index=free.indexOf(frame);if(index>=0)free.splice(index,1);header.copy(frame);at=14;head=0;}const count=Math.min(frame.length-at,chunk.length-offset);chunk.copy(frame,at,offset,offset+count);at+=count;offset+=count;if(at===frame.length){const complete=frame;frame=null;at=0;emit(complete,()=>{if(free.length<2)free.push(complete);});}}};}
async function open(window,{drawMouse=false,launch=spawn,onFailure=()=>{},onState=()=>{},readyMs=6000}={}){
 const child=launch('python3',['-u',path.join(__dirname,'remote/composite-surface.py'),String(window.id),String(window.pid),String(window.identity.start),drawMouse?'1':'0'],{stdio:['pipe','pipe','pipe'],env:{...process.env,DISPLAY:window.display||process.env.DISPLAY}});
 let closed=false,active=null,stderr='',lines='',ready=false,resolveReady,rejectReady;
 const promise=new Promise((resolve,reject)=>{resolveReady=resolve;rejectReady=reject;});
 const command=value=>{if(!closed&&child.stdin.writable)child.stdin.write(JSON.stringify(value)+'\n');};
 const fail=error=>{if(closed)return;if(!ready)rejectReady(error);else onFailure(error);active?.stdin.end();};
 child.stdin.on('error',fail);child.on('error',fail);
 child.stderr.on('data',data=>{stderr=(stderr+data).slice(-4000);lines+=data;while(lines.includes('\n')){const at=lines.indexOf('\n'),line=lines.slice(0,at);lines=lines.slice(at+1);try{const state=JSON.parse(line);if(state.ready){onState(state);if(!ready){ready=true;resolveReady();}}}catch{}}if(lines.length>4000)lines='';});
 child.on('exit',(code,signal)=>fail(Error(stderr.trim()||`Offscreen capture ended (${code??signal})`)));
 const receive=bmpFrames((frame,recycle)=>{const video=active;if(!video?.stdin.writable||video.stdin.writableNeedDrain||video.stdin.writableLength){recycle();return;}video.stdin.write(frame,recycle);});
 child.stdout.on('data',bytes=>{try{receive(bytes);}catch(error){fail(error);child.kill('SIGTERM');}});
 const timer=setTimeout(()=>rejectReady(Error('Offscreen window capture did not initialize')),readyMs);
 async function close(){if(closed)return;command({enabled:false});closed=true;active=null;child.stdin.end();await new Promise(resolve=>{if(child.exitCode!==null||child.signalCode)return resolve();const timer=setTimeout(()=>{child.kill('SIGTERM');resolve();},1000);child.once('exit',()=>{clearTimeout(timer);resolve();});});}
 try{await promise;}catch(error){await close();throw error;}finally{clearTimeout(timer);}
 return {close,start(args,profile){if(closed)throw Error('Offscreen capture is closed');const video=launch('/usr/bin/ffmpeg',scalerArgs(args,profile),{stdio:['pipe','pipe','pipe']});active=video;video.stdin.on('error',()=>{});video.on('exit',()=>{if(active===video){active=null;command({enabled:false});}});command({enabled:true,fps:profile.fps});return video;}};
}
module.exports={open,scalerArgs,bmpFrames};
