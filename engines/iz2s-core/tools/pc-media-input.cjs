'use strict';

const {execFile} = require('node:child_process');
const {promisify} = require('node:util');
const {setTimeout:delay} = require('node:timers/promises');
const {inputBytes} = require('./pc-stream.cjs');

const execute=promisify(execFile);
const neutral=Buffer.alloc(inputBytes);

async function focusedWindow(display){
  const {stdout}=await execute('/usr/bin/xprop',['-root','_NET_ACTIVE_WINDOW'],{
    env:{...process.env,DISPLAY:display},timeout:200,maxBuffer:4096,
  });
  return Number.parseInt(stdout.match(/0x[0-9a-f]+\s*$/i)?.[0]??'',16);
}

function windowInput(capture,gamepad,{focus=focusedWindow,now=()=>performance.now()}={}){
  let focused=false,checked=-Infinity;
  const release=()=>gamepad.release?gamepad.release():gamepad.input(neutral);
  return {
    async input(packet){
      const window=capture.report.window;
      if(!window||capture.report.closed||capture.report.sourceHidden||capture.report.captureSuspended){
        focused=false;checked=-Infinity;return release();
      }
      if(now()-checked>=50){
        focused=await focus(window.display).then(id=>id===window.id,()=>false);
        checked=now();
      }
      if(!focused||capture.report.closed||capture.report.sourceHidden||capture.report.captureSuspended)return release();
      return gamepad.input(packet);
    },
    release,
  };
}

async function forward(stream,origin,target,signal){
  let released=true;
  async function release(){
    if(released)return;
    released=true;
    await target.release();
  }
  try {
    while(!signal.aborted){
      let packet;
      try {
        const result=await fetch(stream+'/input',{headers:{origin},signal:AbortSignal.any([signal,AbortSignal.timeout(200)])});
        if(result.ok){
          const {packet:encoded}=await result.json();
          if(typeof encoded==='string'&&encoded.length===528)packet=Buffer.from(encoded,'base64');
        }
      } catch { /* The absence of a fresh sample releases the target. */ }
      if(!signal.aborted&&packet?.length===inputBytes&&packet.readUInt32LE(328)===2&&!packet.readUInt32LE(336)){
        released=false;
        await target.input(packet);
      }else await release();
      await delay(16,null,{signal});
    }
  } catch(error) {
    if(!signal.aborted)throw error;
  } finally { await release(); }
}

module.exports={windowInput,forward};
