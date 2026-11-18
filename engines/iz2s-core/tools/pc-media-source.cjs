'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const path = require('node:path');
const {setTimeout:delay} = require('node:timers/promises');
const runtime = require('../runtime.cjs');
const {pcmBytes,blitbunProfile} = require('./pc-stream.cjs');
const {forward,windowInput} = require('./pc-media-input.cjs');

const PC_LINK = 'http://127.0.0.1:39525';
const {resolve: moduleFile} = require('../../../core/module-files.cjs');
const service = require('./pc-link-service.cjs');

async function encoder() {
  const codec=require(moduleFile('blitbun','server/lib/pc-codec.js'));
  codec.installWasm(await fs.readFile(moduleFile('blitbun','server/lib/frame-codec.wasm')));
  return frame=>Buffer.from(codec.encode(frame.rgba,frame.w,frame.h,1,{bgra:frame.bgra===true}));
}

async function send(url,origin,body,signal) {
  const response=await fetch(url,{method:'POST',headers:{origin,'content-type':'application/octet-stream'},body,
    signal:AbortSignal.any([signal,AbortSignal.timeout(5000)])});
  const result=await response.json();
  if(!response.ok)throw Error(result.error||'PC Link rejected the media data.');
  return result;
}

async function sendAudio(stream,origin,source,signal) {
  while(!signal.aborted){
    const pcm=source.take(pcmBytes);
    if(!pcm){await delay(8,null,{signal});continue;}
    await send(stream+'/pcm',origin,pcm,signal);
  }
}

async function pump(source,capture,{origin=PC_LINK,signal,encode,input,onFirstFrame,onHealthy,
  stallMs=5000,stableMs=30000}={}) {
  assert(['blitbun','vacootube'].includes(source),'Unknown media source.');
  const stream=origin+'/stream/'+source;
  const ending=new AbortController();
  const active=signal?AbortSignal.any([signal,ending.signal]):ending.signal;
  let afterSequence=-1,frames=0;
  let firstAt=0,healthy=false,deadline=null;
  const warn=(channel,error)=>{
    if(!active.aborted)process.stderr.write(source+' '+channel+': '+error.message+'\n');
  };
  const inputTask=input&&forward(stream,origin,input,active).catch(error=>warn('input',error));
  const audioTask=!capture.report.audioMuted&&capture.audio&&sendAudio(stream,origin,capture.audio,active).catch(error=>warn('audio',error));
  try {
    encode??=await encoder();
    while(!active.aborted){
      const frameSignal=deadline?AbortSignal.any([active,deadline]):active;
      let frame;
      try{frame=await capture.waitFrame({afterSequence,signal:frameSignal});}
      catch(error){if(deadline?.aborted&&!active.aborted)throw Error('No new video frame for 5 seconds.');throw error;}
      if(!frame||active.aborted)break;
      afterSequence=frame.sequence;
      if(frame.sourceEpoch!==capture.report.sourceEpoch)continue;
      const {profile}=await send(`${stream}?width=${frame.w}&height=${frame.h}&format=1`,origin,encode(frame),frameSignal);
      if(source==='blitbun'&&profile&&frame.w!==profile.width&&capture.setProfile)
        await capture.setProfile(blitbunProfile(profile.width));
      deadline=AbortSignal.timeout(stallMs);
      if(++frames===1){firstAt=performance.now();onFirstFrame?.();}
      else if(!healthy&&performance.now()-firstAt>=stableMs){healthy=true;onHealthy?.();}
    }
  } catch(error) {
    if(!active.aborted)throw error;
  } finally {
    ending.abort();
    await Promise.all([audioTask,inputTask]);
    await Promise.allSettled([
      capture.close(),
      fetch(stream,{method:'DELETE',headers:{origin},signal:AbortSignal.timeout(5000)}),
    ]);
  }
  return {frames};
}

async function main(args) {
  const [source,selection]=args;
  if(source==='blitbun'&&selection==='--list'&&args.length===2){
    const windows=require(moduleFile('blitbun','server/lib/native-window-capture.cjs'));
    process.stdout.write(JSON.stringify(await windows.list(),null,2)+'\n');
    return;
  }
  const controllerEnabled=args[2]==='--controller';
  assert(source==='blitbun'&&(args.length===2||args.length===3&&controllerEnabled)&&/^\d+$/.test(selection)||
    source==='vacootube'&&args.length===1,'Usage: pc-media-source.cjs blitbun --list | blitbun <window-id> [--controller] | vacootube');
  const controller=new AbortController();
  const stop=()=>controller.abort();
  process.once('SIGINT',stop);
  process.once('SIGTERM',stop);
  let gamepad,capture;
  try {
    const status=process.env.IZ2S_PC_MEDIA_EXISTING_LINK==='1'?await service.status():await service.ensure();
    const root=path.resolve(__dirname,'../../..');
    assert(status?.service?.root===root,'Start PC Link from this 0.25 installation.');
    const logs=runtime.logs();
    await fs.mkdir(logs,{recursive:true});
    const folder=await fs.mkdtemp(path.join(logs,source+'-'));
    let input;
    if(source==='blitbun'){
      const windows=require(moduleFile('blitbun','server/lib/native-window-capture.cjs'));
      const window=await windows.select(Number(selection));
      if(controllerEnabled){
        const controllers=await require('../../../core/modules.cjs').metadata(root);
        assert(controllers.modules.some(m=>m.id==='controllers'),'Install the controllers module to enable BlitBun gamepad forwarding.');
        gamepad=await require('../../../core/services/remote-gamepad.cjs').create(folder);
      }
      capture=await windows.create(folder,{window,profile:blitbunProfile(status.blitbunProfile?.width),shareSound:true});
      if(gamepad)input=windowInput(capture,gamepad);
    }else {
      capture=await require(moduleFile('vacootube','server/remote-youtube.cjs')).create(folder);
      input={input:capture.input,release:capture.release};
    }
    const ownedCapture=capture;
    capture=null;
    const result=await pump(source,ownedCapture,{signal:controller.signal,input,
      onFirstFrame:()=>process.stdout.write(JSON.stringify({source,folder,state:'publishing media',
        audio:ownedCapture.report.audioMuted?'muted':ownedCapture.audio?'enabled':'unavailable'})+'\n'),
      onHealthy:()=>process.stdout.write(JSON.stringify({source,state:'stable'})+'\n'),
    });
    process.stdout.write(JSON.stringify(result)+'\n');
  } finally {
    process.removeListener('SIGINT',stop);
    process.removeListener('SIGTERM',stop);
    try { await capture?.close(); } finally { await gamepad?.close(); }
  }
}

if(require.main===module)main(process.argv.slice(2)).catch(error=>{
  process.stdout.write(JSON.stringify({source:process.argv[2],state:'error',error:error.message})+'\n');
  console.error(error.message);process.exitCode=1;
});
module.exports={pump,main};
