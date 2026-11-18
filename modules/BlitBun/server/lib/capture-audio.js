(function(root){
 function start(stream,{key,onState=()=>{},onError=()=>{}}){
  let stopped=false,context,node,source,id,latest,busy=false,reported=false;
  function failure(error,message='Audio unavailable'){
   if(stopped)return;onState(message);onError(error);
   if(!reported){reported=true;fetch('/capture-state',{method:'POST',headers:{'x-stream-key':key,'content-type':'application/json'},body:JSON.stringify({phase:'audio-error',error:String(error?.message||error).slice(0,500)})}).catch(()=>{});}
  }
  const headers=()=>({'x-stream-key':key,'x-audio-id':id||''});
  const stopRemote=()=>{if(id){const audioId=id;id=null;fetch('/audio/stop',{method:'POST',headers:{'x-stream-key':key,'x-audio-id':audioId},keepalive:true}).catch(()=>{});}};
  function close(){stopped=true;latest=null;if(node)node.port.onmessage=null;source?.disconnect();node?.disconnect();context?.close().catch(()=>{});stopRemote();}
  async function drain(){
   if(busy||stopped)return;busy=true;
   try{while(latest&&!stopped){const bytes=latest;latest=null;const r=await fetch('/audio',{method:'POST',headers:headers(),body:bytes,signal:AbortSignal.timeout(2000)});if(!r.ok)throw Error('Audio upload failed');}}
   catch(error){failure(error);close();}finally{busy=false;}
  }
  const ready=(async()=>{
   const tracks=stream.getAudioTracks();if(!tracks.length){failure(Error('The browser supplied no audio track. Use native window capture for application sound.'),'No audio track · use native window capture');return;}
   context=new AudioContext({sampleRate:48000,latencyHint:'interactive'});if(context.sampleRate!==48000)throw Error('Unsupported audio sample rate');
   await context.audioWorklet.addModule('/pcm-capture-worklet.js');if(stopped)return;
   source=context.createMediaStreamSource(new MediaStream(tracks));node=new AudioWorkletNode(context,'iz-pcm-capture',{numberOfInputs:1,numberOfOutputs:1,outputChannelCount:[2]});
   const response=await fetch('/audio/start',{method:'POST',headers:headers()});const result=await response.json();if(!response.ok)throw Error(result.error||'Audio output unavailable');id=result.audioId;if(stopped){stopRemote();return;}
   node.port.onmessage=({data})=>{if(stopped)return;latest=data;drain();};source.connect(node);node.connect(context.destination);await context.resume();if(!stopped)onState('Audio');
  })().catch(error=>{failure(error);close();});
  return {ready,close};
 }
 root.IZAudio={start};
})(typeof globalThis==='object'?globalThis:this);
