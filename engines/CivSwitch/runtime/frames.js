// A bounded batch runs controller sampling and the original IZlauncher command
// buffers on the console. No SB4 thread gadgets or filesystem service.
if(window.__iz2sCivRuntime.native&&window.__iz2sCivRuntime.frameRevision!==8){
  window.__iz2sCivRuntime.frame=(plan,count)=>{
    if(!Number.isInteger(count)||count<1||count>6||!Array.isArray(plan.commands)||!plan.commands.length||plan.commands.length>8||plan.commands.length!==plan.semaphores.length)
      throw Error('Invalid bounded graphics frame batch');
    const runtime=window.__iz2sCivRuntime,native=runtime.native,c=BigInt(plan.control),mapped=BigInt(plan.mapped);
    // Empty writes validate ownership without changing memory. Every native
    // structure and HID result must remain in the renderer's allocations.
    for(const address of [c,c+4095n,mapped,mapped+1023n])runtime.dispatch({command:'write',address:String(address),hex:''});
    const call=(module,name,args)=>BigInt(native.dispatch({command:'call',module,name,args:args.map(String)}));
    const vk=(name,args)=>{const result=call('graphics',name,args);if(result!==0n&&result!==1000001003n)throw Error(name+' failed: '+result);};
    const stateNames={1:'_ZN2nn3hid12GetNpadStateEPNS0_16NpadFullKeyStateERKj',4:'_ZN2nn3hid12GetNpadStateEPNS0_16NpadJoyDualStateERKj',
      8:'_ZN2nn3hid12GetNpadStateEPNS0_16NpadJoyLeftStateERKj',16:'_ZN2nn3hid12GetNpadStateEPNS0_17NpadJoyRightStateERKj'};
    let frequency=plan.tickFrequency;
    if(frequency===undefined){
      if(!runtime.clockFrequency){
        for(const name of ['_ZN2nn2os13GetSystemTickEv','_ZN2nn2os22GetSystemTickFrequencyEv'])native.dispatch({command:'lookup',name,module:'sdk'});
        runtime.clockFrequency=Number(call('sdk','_ZN2nn2os22GetSystemTickFrequencyEv',[]));
      }
      frequency=runtime.clockFrequency;
    }
    if(!Number.isSafeInteger(frequency)||frequency<1)throw Error('Invalid Civ7 hardware tick frequency');
    runtime.dispatch({command:'validate-owned',address:String(mapped+0x700d70n),size:16});
    const gestureKey=plan.exitGesture===true?String(mapped)+':'+plan.commands.join(','):null;
    if(runtime.exitGesture?.key!==gestureKey)runtime.exitGesture={key:gestureKey,held:false,neutral:false,last:null};
    const gesture=runtime.exitGesture;
    for(let frame=0;frame<count;frame++){
      const handheld=Number(call('sdk','_ZN2nn3hid15GetNpadStyleSetERKj',[c+0x504n]));
      if(handheld&2)call('sdk','_ZN2nn3hid12GetNpadStateEPNS0_17NpadHandheldStateERKj',[mapped,c+0x504n]);
      else{
        const style=Number(call('sdk','_ZN2nn3hid15GetNpadStyleSetERKj',[c+0x500n]));
        const selected=[1,4,8,16].find(bit=>style&bit);
        if(selected)call('sdk',stateNames[selected],[mapped,c+0x500n]);
        else for(let offset=0;offset<64;offset+=8)write64(mapped+BigInt(offset),0n);
      }
      call('sdk','_ZN2nn3hid19GetTouchScreenStateILm16EEEvPNS0_16TouchScreenStateIXT_EEE',[mapped+128n]);
      if(gestureKey){
        const hardwareTick=call('sdk','_ZN2nn2os13GetSystemTickEv',[])*31250000n/BigInt(frequency);
        const held=(Number(read32(mapped+8n))&32)!==0;
        if(!held)gesture.neutral=true;
        if(held&&!gesture.held&&gesture.neutral){
          const double=gesture.last!==null&&hardwareTick>gesture.last&&hardwareTick-gesture.last<=15625000n;
          gesture.last=double?null:hardwareTick;
          if(double){
            for(let byte=0;byte<4;byte++)write8(mapped+1016n+BigInt(byte),byte===0?1n:0n);
            return {frames:frame,buttons:Number(read32(mapped+8n)),selected:Number(read32(mapped+80n)),launch:0,exit:1,menuGesture:'double-r3'};
          }
        }
        gesture.held=held;
      }
      vk('vkAcquireNextImage2KHR',[plan.device,c+0x100n,c]);
      const image=Number(read32(c));if(image>=plan.commands.length)throw Error('Invalid acquired swapchain image');
      write64(c+0x400n,BigInt(plan.commands[image]));write64(c+0x408n,BigInt(plan.semaphores[image]));
      // Acquiring a display image can wait for vsync or the preceding frame.
      // Publish time after that wait so the audio producer does not schedule
      // an already expired PCM window using an old timestamp.
      const hardwareTick=call('sdk','_ZN2nn2os13GetSystemTickEv',[])*31250000n/BigInt(frequency);
      const tick=runtime.audioClock?runtime.audioClock.sample(hardwareTick):hardwareTick;
      runtime.lastFrameTick=tick;
      write64(mapped+0x700d70n,tick);write64(mapped+0x700d78n,31250000n);
      vk('vkQueueSubmit',[plan.queue,1,c+0x200n,0]);
      vk('vkQueuePresentKHR',[plan.queue,c+0x300n]);
      vk('vkQueueWaitIdle',[plan.queue]);
      runtime.audioQueue?.pump(mapped);
    }
    return {frames:count,buttons:Number(read32(mapped+8n)),selected:Number(read32(mapped+80n)),
      launch:Number(read32(mapped+1012n)),exit:Number(read32(mapped+1016n)),audioClock:runtime.audioClock?.status()||null,audioQueue:runtime.audioQueue?.status()||null};
  };
}

window.__iz2sCivRuntime.frameRevision=8;
