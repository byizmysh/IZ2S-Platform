// Keep the Civ7 presentation clock aligned with the running AudioOut device.
// Observe the named SDK counter first; never infer audio progress from FPS.
if(window.__iz2sCivRuntime.native&&window.__iz2sCivRuntime.audioClockRevision!==1){
 const runtime=window.__iz2sCivRuntime,previous=runtime.dispatch,native=runtime.native;
 const names={start:'_ZN2nn5audio13StartAudioOutEPNS0_8AudioOutE',stop:'_ZN2nn5audio12StopAudioOutEPNS0_8AudioOutE',
  close:'_ZN2nn5audio13CloseAudioOutEPNS0_8AudioOutE',played:'_ZN2nn5audio28GetAudioOutPlayedSampleCountEPKNS0_8AudioOutE',
  rate:'_ZN2nn5audio21GetAudioOutSampleRateEPKNS0_8AudioOutE',tick:'_ZN2nn2os13GetSystemTickEv',frequency:'_ZN2nn2os22GetSystemTickFrequencyEv'};
 const call=(name,args=[])=>BigInt(native.dispatch({command:'call',module:'sdk',name,args:args.map(String)}));
 let mode='off',state=null,unavailable=null;
 const summary=state=>state?{mode,running:state.running,qualified:state.qualified,deviceFrames:String(state.lastCount-state.firstCount),
  driftMilliseconds:state.driftMilliseconds||0,error:state.error||null}:{mode,running:false,error:unavailable};
 runtime.audioClock={
  status:()=>summary(state),
  sample(wallTick){
   if(!state?.running)return wallTick;
   const alignedWall=wallTick-state.wallAnchor+state.anchor;
   if(state.error){const fallback=mode==='device'?alignedWall:wallTick;state.published=fallback>state.published?fallback:state.published;return state.published;}
   try{
    if(!native.owned(state.handle,256))throw Error('AudioOut metadata is no longer owned');
    const count=call(names.played,[state.handle]);
    if(count<state.lastCount||wallTick<state.wallAnchor)throw Error('Audio device counter moved backwards');
    if(count-state.lastCount>(wallTick-state.lastWall)*state.rate/31250000n+4096n)throw Error('Audio device counter jumped outside its rate bound');
    if(count!==state.lastCount)state.lastAdvance=wallTick;
    state.lastCount=count;state.lastWall=wallTick;
    const elapsed=wallTick-state.wallAnchor,played=count-state.firstCount;
    const deviceTick=state.anchor+played*31250000n/state.rate;
    state.driftMilliseconds=Number(alignedWall-deviceTick)/31250;
    // Audio DSP counters can advance in blocks. Qualify the unit/rate over
    // at least half a second, with room for one normal device block.
    if(!state.qualified&&elapsed>=15625000n){
     const expected=elapsed*state.rate/31250000n;
     if(played*5n<expected*4n||played*5n>expected*6n)throw Error('Audio counter rate differs from the opened format');
     state.qualified=true;
    }
    if(wallTick-state.lastAdvance>15625000n)throw Error('Audio device counter stopped');
    if(mode==='observe')return wallTick;
    const tick=state.qualified?deviceTick:alignedWall;
    // Changing clock source must never send application time backwards.
    state.published=tick>state.published?tick:state.published;
    return state.published;
   }catch(error){state.error=String(error.message||error);const fallback=mode==='device'?alignedWall:wallTick;state.published=fallback>state.published?fallback:state.published;return state.published;}
  },
 };
 runtime.dispatch=request=>{
  if(request.command==='audio-clock-mode'){
   if(!['off','observe','device'].includes(request.mode))throw Error('Invalid Civ7 audio clock mode');
   if(state?.running)throw Error('Stop AudioOut before changing its clock');
   mode=request.mode;state=null;unavailable=null;return {mode};
  }
  if(request.command==='info')return {...previous(request),audioClock:summary(state)};
  const name=request.command==='call'&&request.module==='sdk'?request.name:null;
  let prepared=false;
  if(name===names.start&&mode!=='off'){
   try{
    const handle=BigInt(request.args[0]);
    if(!native.owned(handle,256))throw Error('AudioOut metadata is not owned');
    for(const symbol of [names.played,names.rate,names.tick,names.frequency])native.dispatch({command:'lookup',module:'sdk',name:symbol});
    prepared=true;
   }catch(error){state=null;unavailable=String(error.message||error);}
  }
  // Preserve the original native result, including failure. The observer may
  // neither repeat a Start nor change ownership if its own query fails.
  const result=previous(request);
  if(name===names.start&&BigInt(result)===0n&&prepared){
   try{
    const handle=BigInt(request.args[0]),rate=call(names.rate,[handle]),frequency=call(names.frequency);
    if(rate<8000n||rate>192000n||frequency<1n)throw Error('Invalid AudioOut clock geometry');
    const count=call(names.played,[handle]),wallAnchor=call(names.tick)*31250000n/frequency;
    // The app's Start handshake refers to its last completed frame. Align
    // sample zero to that exact timestamp, not to a later PC round trip.
    const anchor=runtime.lastFrameTick===undefined?wallAnchor:runtime.lastFrameTick;
    state={handle,rate,firstCount:count,lastCount:count,anchor,wallAnchor,published:anchor,lastWall:wallAnchor,lastAdvance:wallAnchor,running:true,qualified:false};
   }catch(error){state=null;unavailable=String(error.message||error);}
  }
  if((name===names.stop||name===names.close)&&state&&String(request.args[0])===String(state.handle)&&BigInt(result)===0n)state.running=false;
  return result;
 };
 runtime.audioClockRevision=1;
}
