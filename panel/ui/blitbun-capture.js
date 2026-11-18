'use strict';

globalThis.IZ2S_BROWSER_CAPTURE = function create({key,api,changed,feedback,preview}) {
  const route='/api/iz2s-core/media/browser/';
  const pause=ms=>new Promise(resolve=>setTimeout(resolve,ms));

  let active;
  const onPageHide=()=>{if(active)void cleanup(active);};
  async function packet(session,kind,bytes,width) {
    const response=await fetch(route+kind,{method:'POST',headers:{
      'x-iz-key':key,'x-blitbun-capture':session.id,'content-type':'application/octet-stream',
      ...(width?{'x-blitbun-width':String(width)}:{}),
    },body:bytes,signal:AbortSignal.any([session.controller.signal,AbortSignal.timeout(4000)])});
    if(!response.ok) {
      const result=await response.json().catch(()=>null);
      throw Error(result?.error||'Could not send browser media to PC Link.');
    }
    return response.json();
  }
  async function cleanup(session) {
    if(active===session){active=undefined;removeEventListener('pagehide',onPageHide);}
    session.controller.abort();
    session.encoder?.terminate();
    session.stream?.getTracks().forEach(track=>track.stop());
    if(session.video){
      session.video.srcObject=null;
      session.video.remove();
      if(preview&&!preview.childElementCount)preview.hidden=true;
    }
    session.audio?.close().catch(()=>{});
    if(session.id) {
      const id=session.id;
      session.id=null;
      await fetch(route+'stop',{method:'POST',headers:{
        'x-iz-key':key,'content-type':'application/json',
      },body:JSON.stringify({id}),keepalive:true,signal:AbortSignal.timeout(5000)}).catch(()=>{});
    }
    changed();
  }
  async function audio(session) {
    const tracks=session.stream.getAudioTracks();
    if(!tracks.length)return;
    const context=session.audio=new AudioContext({sampleRate:48000});
    if(context.sampleRate!==48000)throw Error('Unsupported audio sample rate');
    await context.audioWorklet.addModule('/blitbun-pcm.js');
    if(active!==session)return;
    const input=context.createMediaStreamSource(new MediaStream(tracks));
    const worklet=new AudioWorkletNode(context,'iz-pcm-capture',{
      outputChannelCount:[2],processorOptions:{lowLatency:true},
    });
    const packetBytes=3840,pending=[];
    let sending=false;
    async function flush() {
      if(sending)return;
      sending=true;
      try {
        while(pending.length&&active===session) {
          const {bytes,time}=pending.shift();
          if(performance.now()-time<=80)await packet(session,'audio',bytes);
        }
      } catch(error) {
        if(active===session){session.audioFailed=true;changed();}
        await context.close().catch(()=>{});
      } finally {sending=false;}
    }
    worklet.port.onmessage=({data})=>{
      if(data.byteLength!==packetBytes)return;
      if(pending.length===4)pending.shift();
      pending.push({bytes:data,time:performance.now()});
      void flush();
    };
    input.connect(worklet).connect(context.destination);
    await context.resume();
  }
  function encodeFrame(worker, {bitmap,rgba}, width, height, signal) {
    return new Promise((resolve,reject)=>{
      signal.throwIfAborted();
      const finish=(error,encoded)=>{
        signal.removeEventListener('abort',cancel);
        worker.onmessage=worker.onerror=null;
        if(error)reject(error);
        else resolve(encoded);
      };
      const cancel=()=>finish(signal.reason);
      signal.addEventListener('abort',cancel,{once:true});
      worker.onmessage=({data})=>finish(data.error?Error(data.error):null,data.encoded);
      worker.onerror=event=>finish(Error(event.message||'The BlitBun frame encoder stopped.'));
      try {worker.postMessage({bitmap,rgba,width,height},[bitmap??rgba.buffer]);}
      catch(error) {finish(error);}
    });
  }

  async function playVideo(video, signal) {
    signal.throwIfAborted();
    const deadline=AbortSignal.timeout(8000);
    const cancellation=AbortSignal.any([signal,deadline]);
    let abort;
    try {
      await Promise.race([
        video.play(),
        new Promise((_,reject)=>{
          abort=()=>reject(cancellation.reason);
          cancellation.addEventListener('abort',abort,{once:true});
        }),
      ]);
    } catch(error) {
      if(deadline.aborted&&!signal.aborted)throw Error('The selected tab did not send video. Try a different tab.');
      throw error;
    } finally {
      if(abort)cancellation.removeEventListener('abort',abort);
    }
  }

  async function start() {
    if(active)return;
    const session=active={controller:new AbortController(),phase:'choosing',audioFailed:false};
    addEventListener('pagehide',onPageHide);
    changed();
    try {
      // The browser permission dialog must open directly from the button click.
      const focus=typeof CaptureController==='function' ? new CaptureController() : null;
      focus?.setFocusBehavior?.('focus-capturing-application');
      session.stream=await navigator.mediaDevices.getDisplayMedia({
        video:{displaySurface:'browser',frameRate:{ideal:60,max:60}},audio:true,
        selfBrowserSurface:'exclude',systemAudio:'exclude',windowAudio:'exclude',surfaceSwitching:'include',
        ...(focus ? {controller:focus} : {}),
      });
      if(active!==session)return;
      const track=session.stream.getVideoTracks()[0];
      const surface=track?.getSettings().displaySurface;
      if(!track||(surface&&surface!=='browser'))throw Error('Choose a browser tab in the sharing dialog.');
      track.addEventListener('ended',()=>{
        if(active!==session)return;
        if(session.phase!=='running')feedback('Browser capture ended. Choose a browser tab again.');
        void cleanup(session);
      },{once:true});
      session.phase='starting';
      changed();
      const video=session.video=document.createElement('video');
      video.muted=true;
      video.playsInline=true;
      if(preview){
        preview.append(video);
        preview.hidden=false;
      }
      video.srcObject=session.stream;
      await playVideo(video,session.controller.signal);
      if(active!==session)return;
      let profile=await api(route+'begin',{});
      session.id=profile.id;
      if(active!==session)return;
      let workerDraw=typeof VideoFrame==='function'&&typeof OffscreenCanvas==='function';
      let canvas,ctx,encodedWidth=0;
      const encoder=session.encoder=new Worker('/blitbun-encoder.js');
      void audio(session).catch(()=>{
        if(active===session){session.audioFailed=true;changed();}
      });
      session.audioFailed=!session.stream.getAudioTracks().length;
      let upload,lastResult,deadline=performance.now(),frameClock=0,previousClock=-1,frameCallback,wake,sentAt=-Infinity;
      const watch=()=>{
        frameClock++;
        wake?.();
        frameCallback=video.requestVideoFrameCallback(watch);
      };
      const nextFrame=()=>new Promise(resolve=>{
        const timer=setTimeout(finish,200);
        function finish(){ clearTimeout(timer);wake=undefined;resolve(); }
        wake=finish;
      });
      if(video.requestVideoFrameCallback)frameCallback=video.requestVideoFrameCallback(watch);
      session.controller.signal.addEventListener('abort',()=>{
        if(frameCallback!==undefined)video.cancelVideoFrameCallback(frameCallback);
        wake?.();
      },{once:true});
      while(active===session) {
        const frameStart=performance.now();
        const latest=lastResult;
        if(latest?.error)throw latest.error;
        if(latest?.profile)profile=latest.profile;
        const {width,height,fps=60}=profile;
        // A fresh frame can arrive just after the pacing timer. Wake promptly instead of
        // skipping an entire interval. The timeout keeps static or background tabs alive.
        const sourceClock=frameClock>0&&!document.hidden&&(video.checkVisibility?.()??true);
        if(sourceClock&&frameClock===previousClock&&frameStart-sentAt<180&&encodedWidth===width) {
          await nextFrame();
          continue;
        }
        previousClock=frameClock;
        encodedWidth=width;
        // The browser already limits capture to 60 FPS. Avoid racing another 60 Hz
        // timer against the video callback, which can skip an otherwise fresh frame.
        const pacedByVideo=sourceClock&&fps>=60;
        deadline=pacedByVideo?frameStart:Math.max(deadline+1000/fps,frameStart);
        let encoded;
        if(workerDraw) {
          let bitmap;
          try {bitmap=new VideoFrame(video,{timestamp:0});}
          catch(error) {
            if(error.name!=='NotSupportedError'&&error.name!=='SecurityError')throw error;
            workerDraw=false;
          }
          if(bitmap) {
            try {encoded=await encodeFrame(encoder,{bitmap},width,height,session.controller.signal);}
            catch(error) {
              if(error.name!=='DataCloneError')throw error;
              workerDraw=false;
            } finally {bitmap.close();}
          }
        }
        if(!workerDraw) {
          canvas??=document.createElement('canvas');
          ctx??=canvas.getContext('2d',{alpha:false,willReadFrequently:true});
          if(canvas.width!==width||canvas.height!==height){canvas.width=width;canvas.height=height;}
          ctx.imageSmoothingQuality='high';
          const scale=Math.min(width/video.videoWidth,height/video.videoHeight);
          const w=Math.round(video.videoWidth*scale),h=Math.round(video.videoHeight*scale);
          ctx.fillStyle='#000';
          ctx.fillRect(0,0,width,height);
          ctx.drawImage(video,(width-w)/2,(height-h)/2,w,h);
          const rgba=ctx.getImageData(0,0,width,height).data;
          encoded=await encodeFrame(encoder,{rgba},width,height,session.controller.signal);
        }
        // Encode the latest frame while at most one upload is in flight.
        const previous=await upload;
        if(previous?.error)throw previous.error;
        if(previous?.profile)profile=previous.profile;
        if(profile.width===width) {
          sentAt=performance.now();
          upload=packet(session,'frame',encoded,width).then(latest=>{
            if(active===session&&session.phase!=='running'){
              session.phase='running';
              changed();
            }
            return lastResult=latest;
          },error=>lastResult={error});
        }
        const remaining=deadline-performance.now();
        if(remaining>0)await pause(remaining);
      }
    } catch(error) {
      if(active===session){
        if(error.name==='NotAllowedError')feedback('The browser did not grant access to a tab. Open the page to share in this browser, then select it in the sharing dialog.');
        else if(error.name==='NotFoundError')feedback('No source tab is available. Open the page to share in this browser first.');
        else feedback(error.message);
      }
    } finally {
      await cleanup(session);
    }
  }
  return {
    start,stop:()=>active&&cleanup(active),available:()=>!!navigator.mediaDevices?.getDisplayMedia,
    status:()=>active?{phase:active.phase,audio:active.audioFailed?'muted':null}:null,
  };
};
