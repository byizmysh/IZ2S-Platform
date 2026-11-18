'use strict';

const assert = require('node:assert/strict');
const {createHash,randomBytes} = require('node:crypto');

const sources = new Map([['V','blitbun'],['W','vacootube'],['M','blitbun'],['N','vacootube'],['Y','blitbun'],['Z','vacootube'],['y','blitbun'],['a','blitbun'],['b','blitbun']]);
const withAudio = new Set(['M','N','Y','Z','y','a','b']);
const withInput = new Set(['Y','Z','y']);
const inputBytes = 396;
const inputAgeMs = 220;
const maximumAgeMs = 1500;
const pcmBytes = 3840;
const maxPcmBatch = 3;
const pcmQueueLimit = 5;
const pcmAgeMs = 250;
// Keep a small jitter cushion without replaying old sound after a stall.
const browserPcmQueueLimit = 6;
const browserPcmStartPackets = 2;
const browserPcmAgeMs = 120;
const browserAudioPriorityPackets = 4;
const browserVideoMaxWaitMs = 50;
const blitbunWidths = [640,832,960,1280,1600,1920];
function blitbunProfile(width=960) {
  assert(blitbunWidths.includes(width),'Unsupported BlitBun resolution.');
  return {width,height:Math.ceil(width*9/64)*4,fps:width<=1280?60:width<=1600?45:30};
}

function frameSize(width,height,format) {
  assert([width,height,format].every(Number.isSafeInteger),'Invalid frame dimensions.');
  if(format===0&&width>=2&&width<=640&&height>=2&&height<=360&&width%2===0)return width*height*2;
  if(width>=4&&height>=4&&width%4===0&&height%4===0){
    if(format===1&&width<=1920&&height<=1080)return width*height/2;
    if(format===2&&width<=1280&&height<=720&&1024+width*height<=0x78000)return 1024+width*height;
  }
  throw Error('Unsupported frame size or format.');
}

function create({now=()=>performance.now()}={}) {
  const frames=new Map();
  const audio=new Map();
  const input=new Map();
  const uploading=new Set();
  let browserPrimed=false,browserAudio=false;
  let lastBlitbunVideoAt=-Infinity, blitbunTransport=null, blitbunAudioBatchAt=-Infinity;
  const offeredBlitbun=new Map();
  let confirmedBlitbun;
  const blitbunAcks=[];
  function offerBlitbun(frame) {
    if(offeredBlitbun.has(frame.sequence))return;
    offeredBlitbun.set(frame.sequence,{width:frame.bytes.readUInt32BE(4),height:frame.bytes.readUInt32BE(8),expires:frame.expires});
    if(offeredBlitbun.size>12)offeredBlitbun.delete(offeredBlitbun.keys().next().value);
  }
  let sequence=randomBytes(4).readUInt32BE();
  let profile=blitbunProfile(), lastResolutionCommand=0;

  function nextSequence(){sequence=(sequence+1)>>>0||1;return sequence;}

  function publish(source,width,height,format,pixels,browser) {
    assert(Buffer.isBuffer(pixels)&&pixels.length===frameSize(width,height,format),'Invalid frame length.');
    nextSequence();
    const bytes=Buffer.allocUnsafe(16+pixels.length);
    [sequence,width,height,format].forEach((value,index)=>bytes.writeUInt32BE(value,index*4));
    pixels.copy(bytes,16);
    frames.set(source,{sequence,bytes,browser,expires:now()+maximumAgeMs});
    return {sequence};
  }

  function response(operation,seen,heard,packet) {
    const source=sources.get(operation);
    if(source==='blitbun'&&packet?.length===inputBytes&&packet.readUInt32LE(328)===2&&!packet.readUInt32LE(336)) {
      const command=(packet.readUInt32LE(332)>>>9)&127;
      const index=(command&7)-1;
      if(!command)lastResolutionCommand=0;
      if((command>>>3)&&index>=0&&index<blitbunWidths.length&&command!==lastResolutionCommand) {
        lastResolutionCommand=command;
        profile=blitbunProfile(blitbunWidths[index]);
      }
    }
    const frame=frames.get(source);
    const time=now();
    const batchAudio=operation==='b';
    const audioOnly=batchAudio||operation==='a',videoOnly=operation==='y';
    if(batchAudio)blitbunAudioBatchAt=time;
    if(operation==='Y'||videoOnly)blitbunTransport={mode:videoOnly?'split':'combined',at:time};
    if(!audioOnly&&(!frame||time>=frame.expires))return {status:0};
    if(!audioOnly&&source==='blitbun'&&seen&&confirmedBlitbun?.sequence!==seen) {
      const offered=offeredBlitbun.get(seen);
      if(offered&&time<offered.expires) {
        const prior=blitbunAcks.at(-1);
        if(confirmedBlitbun?.width!==offered.width||prior!==undefined&&time-prior>250)
          blitbunAcks.length=0;
        confirmedBlitbun={...offered,sequence:seen};
        blitbunAcks.push(time);
        while(blitbunAcks.length>1&&blitbunAcks[0]<time-1100)blitbunAcks.shift();
      }
    }
    if(withInput.has(operation)&&packet?.length===inputBytes)
      input.set(source,{data:Buffer.from(packet),expires:now()+inputAgeMs});
    if(withAudio.has(operation)){
      let pcm=[];
      let video=!audioOnly&&frame.sequence!==seen?frame.bytes:null;
      if(!videoOnly) {
        const queue=audio.get(source)??[];
        const at=queue.findIndex(packet=>packet.sequence===heard);
        if(at!==-1)queue.splice(0,at+1);
        while(queue.length&&queue[0].expires<=time)queue.shift();
        pcm=queue.slice(0,batchAudio?maxPcmBatch:1);
        if(source==='blitbun'&&browserAudio) {
          if(queue.length>=browserPcmStartPackets)browserPrimed=true;
          if(!browserPrimed)pcm=[];
          // A short PCM reply gives the combined client a chance to catch up.
          // The first frame and regular video updates still get through.
          if(video&&seen&&pcm.length&&queue.length>=browserAudioPriorityPackets&&time-lastBlitbunVideoAt<browserVideoMaxWaitMs)
            video=null;
        }
      }
      if(!pcm.length&&!video)return {status:5};
      const meta=Buffer.alloc(28);
      if(video){
        if(source==='blitbun') {
          offerBlitbun(frame);
          lastBlitbunVideoAt=time;
        }
        video.copy(meta,0,0,16);
        meta.writeUInt32BE(video.length-16,16);
      }
      if(pcm.length) {
        meta.writeUInt32BE(pcm.at(-1).sequence,20);
        meta.writeUInt32BE(pcm.length*pcmBytes,24);
      }
      const bytes=Buffer.concat([meta,...(video?[video.subarray(16)]:[]),...pcm.map(part=>part.bytes)]);
      return {status:6,bytes,digest:createHash('sha256').update(bytes).digest()};
    }
    if(frame.sequence===seen)return {status:5};
    if(source==='blitbun')offerBlitbun(frame);
    frame.digest??=createHash('sha256').update(frame.bytes).digest();
    return {status:4,bytes:frame.bytes,digest:frame.digest};
  }

  async function upload(req,host,json) {
    const url=new URL(req.url,'http://'+host);
    const sink=url.pathname.slice('/stream/'.length);
    const [source,endpoint,extra]=sink.split('/');
    const pcm=endpoint==='pcm';
    const browser=source==='blitbun'&&url.searchParams.get('browser')==='1';
    if(source!=='blitbun'&&source!=='vacootube'){json(404,{error:'Unknown frame source.'});return;}
    if(extra!==undefined||endpoint!==undefined&&!pcm&&endpoint!=='input'&&endpoint!=='profile'){
      json(404,{error:'Unknown media operation.'});return;
    }
    if(req.headers.origin!=='http://'+host){json(403,{error:'Upload frames from the paired PC Link service.'});return;}
    if(endpoint==='profile') {
      if(source!=='blitbun'||!['GET','POST'].includes(req.method))return json(405,{error:'Unsupported stream profile operation.'});
      try {
        if(req.method==='POST')profile=blitbunProfile(Number(url.searchParams.get('width')));
        json(200,profile);
      } catch(error) {json(400,{error:error.message});}
      return;
    }
    if(endpoint==='input'){
      if(req.method!=='GET'){json(405,{error:'Use GET for controller input.'});return;}
      const frame=frames.get(source),sample=input.get(source),time=now();
      json(200,{packet:frame&&sample&&time<frame.expires&&time<sample.expires?sample.data.toString('base64'):null});
      return;
    }
    if(!['POST','DELETE'].includes(req.method)){json(405,{error:'Unsupported frame operation.'});return;}
    try {
      assert(!uploading.has(sink),'An upload is already in progress.');
      if(req.method==='DELETE'){
        assert(pcm||!uploading.has(source+'/pcm'),'An audio upload is already in progress.');
        if(!pcm){frames.delete(source);input.delete(source);}
        audio.delete(source);
        if(source==='blitbun') {
          browserPrimed=browserAudio=false;
          if(!pcm) {
            offeredBlitbun.clear();confirmedBlitbun=undefined;blitbunAcks.length=0;
            lastBlitbunVideoAt=-Infinity;
          }
        }
        json(200,{cleared:true});return;
      }
      assert(req.headers['content-type']==='application/octet-stream','Expected encoded frame data.');
      const dims=pcm?[]:['width','height','format'].map(key=>{
        const value=url.searchParams.get(key);
        assert(value!==null&&/^\d{1,4}$/.test(value),'Invalid frame metadata.');
        return Number(value);
      });
      const bytes=pcm?pcmBytes:frameSize(...dims);
      assert.equal(Number(req.headers['content-length']),bytes,'Invalid frame length.');
      uploading.add(sink);
      try {
        const pixels=Buffer.allocUnsafe(bytes);
        let offset=0;
        for await(const part of req){
          assert(part.length<=bytes-offset,'Unexpected extra frame data.');
          part.copy(pixels,offset);
          offset+=part.length;
        }
        assert.equal(offset,bytes,'Incomplete frame.');
        if(pcm){
          const queue=audio.get(source)??[];
          if(source==='blitbun')browserAudio=browser;
          queue.push({sequence:nextSequence(),bytes:pixels,expires:now()+(browser?browserPcmAgeMs:pcmAgeMs)});
          if(queue.length>(browser?browserPcmQueueLimit:pcmQueueLimit))queue.shift();
          audio.set(source,queue);
          json(200,{queued:queue.length});
        }else {
          const result=publish(source,...dims,pixels,browser);
          json(200,source==='blitbun'?{...result,profile}:result);
        }
      } finally { uploading.delete(sink); }
    } catch(error) { json(400,{error:error.message}); }
  }

  return {response,upload,blitbunProfile:()=>profile,
    blitbunAudioBatch:()=>now()-blitbunAudioBatchAt<1500,
    blitbunTransport:()=>blitbunTransport&&now()-blitbunTransport.at<1500?blitbunTransport.mode:null,
    blitbunFps:()=>{
      const tail=blitbunAcks.at(-1),span=tail-blitbunAcks[0];
      return span>=650&&now()-tail<=250?Math.round((blitbunAcks.length-1)*1000/span):null;
    },
    blitbunVideo:()=>{
      if(!confirmedBlitbun||now()>=confirmedBlitbun.expires)return null;
      const {width,height}=confirmedBlitbun;
      return {width,height};
    },
  };
}

module.exports={create,sources,withAudio,withInput,inputBytes,pcmBytes,blitbunProfile};
