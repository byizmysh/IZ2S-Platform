// The Civ7 DOOM test submits only completed GPU PCM, in short descriptors.
// Refills run locally at a joined graphics boundary, never through PC PCM copies.
if(window.__iz2sCivRuntime.native&&!window.__iz2sCivRuntime.audioQueue){
 const runtime=window.__iz2sCivRuntime,native=runtime.native,previous=runtime.dispatch;
 const names={set:'_ZN2nn5audio21SetAudioOutBufferInfoEPNS0_14AudioOutBufferEPvmm',append:'_ZN2nn5audio20AppendAudioOutBufferEPNS0_8AudioOutEPNS0_14AudioOutBufferE',release:'_ZN2nn5audio25GetReleasedAudioOutBufferEPNS0_8AudioOutE'};
 const call=(name,args)=>BigInt(native.dispatch({command:'call',module:'sdk',name,args:args.map(String)}));
 let q=null;
 const status=()=>q?{active:true,queued:q.pending.size,maxQueuedMilliseconds:4096/48,submitted:q.submitted,released:q.released,droppedFrames:q.dropped,underruns:q.underruns,nextFrame:q.next,error:q.error}:null;
 const fail=error=>{if(q)q.error=String(error.message||error);throw Object.assign(error,{uncertain:true,resourcesRetained:true});};
 const descriptor=i=>q.metadata+0x400n+BigInt(i*64);
 runtime.audioQueue={status,pump(mapped){
  if(!q)return;if(q.error)throw Object.assign(Error(q.error),{uncertain:true});
  try{
   if(mapped!==q.mapped||!native.owned(q.metadata,4096)||!native.owned(q.pcm,4194304)||!native.owned(q.mapped,1024))throw Error('Audio queue storage is no longer owned');
   const control=q.mapped+784n;
   if(Number(read32(control))!==0x44413138)throw Error('Audio producer identity changed');
   const phase=Number(read32(control+36n));if(phase!==3)return;
   for(let i=0;i<4;i++){
    const value=call(names.release,[q.metadata]);if(value===0n)break;
    const index=[...q.pending].find(j=>descriptor(j)===value);
    if(index===undefined)throw Error('Unknown or duplicate short PCM release');
    q.pending.delete(index);q.released++;
   }
   const rebases=Number(read32(control+124n))+Number(read32(control+128n))*4294967296;
   if(!Number.isSafeInteger(rebases)||rebases>1000000)throw Error('Invalid GPU PCM rebase counter');
   const epoch=rebases*16777216,elapsed=epoch+Number(read32(control+84n)),written=epoch+Number(read32(control+92n));
   if(written<elapsed||written-elapsed>1048576)throw Error('Invalid GPU PCM production window');
   if(q.pending.size===0)q.underruns++;
   // After a stall, discard the expired timeline rather than playing a backlog.
   if(q.next<elapsed){const fresh=Math.ceil(elapsed/1024)*1024;q.dropped+=fresh-q.next;q.next=fresh;}
   for(let i=0;i<4;i++){
    if(q.pending.has(i)||q.next+1024>written)continue;
    const offset=q.next%1048576;
    call(names.set,[descriptor(i),q.pcm+BigInt(offset*4),4096n,4096n]);
    if(call(names.append,[q.metadata,descriptor(i)])!==1n)throw Error('Short PCM append rejected');
    q.pending.add(i);q.next+=1024;q.submitted++;
   }
  }catch(error){fail(error);}
 }};
 runtime.dispatch=request=>{
  if(request.command==='audio-queue-open'){
   if(q)throw Error('A short PCM queue is already active');
   const mapped=BigInt(request.mapped),pcm=BigInt(request.pcm),metadata=BigInt(request.metadata);
   if(!native.owned(mapped,1024)||!native.owned(pcm,4194304)||!native.owned(metadata,4096)||metadata%4096n||
      metadata<pcm+4194304n&&metadata+4096n>pcm||metadata<mapped+8388608n&&metadata+4096n>mapped||
      pcm<mapped+8388608n&&pcm+4194304n>mapped||Number(read32(mapped+784n))!==0x44413138||
      Number(read32(mapped+784n+24n))!==1048576||Number(read32(mapped+784n+92n))!==8192)throw Error('Invalid short PCM queue allocation or priming');
   for(const name of Object.values(names))native.dispatch({command:'lookup',module:'sdk',name});
   q={mapped,pcm,metadata,pending:new Set([0,1,2,3]),next:4096,submitted:4,released:0,dropped:0,underruns:0,error:null};return status();
  }
  if(request.command==='audio-queue-detach'){
   if(!q)return null;if(q.error)return fail(Error(q.error));
   const result={...status(),pending:[...q.pending]};q=null;return result;
  }
  if(request.command==='info')return {...previous(request),audioQueue:status()};
  return previous(request);
 };
}
