// Standard amplitude/frequency HD vibration envelopes. Not a claim of PCM/HD Rumble 2 encoding.
function validate({side=0,steps}={}){
 if(!Number.isInteger(side)||side<0||side>2||!Array.isArray(steps)||steps.length<1||steps.length>32)throw Error('Haptics needs side 0/1/2 and 1..32 steps');
 let duration=0;const normalized=steps.map(step=>{
  const v={milliseconds:step.milliseconds,lowAmplitude:step.lowAmplitude??step.amplitude??.15,lowFrequency:step.lowFrequency??160,highAmplitude:step.highAmplitude??step.amplitude??.15,highFrequency:step.highFrequency??320};
  if(!Number.isInteger(v.milliseconds)||v.milliseconds<1||v.milliseconds>500||![v.lowAmplitude,v.highAmplitude].every(x=>Number.isFinite(x)&&x>=0&&x<=1)||![v.lowFrequency,v.highFrequency].every(x=>Number.isFinite(x)&&x>=40&&x<=1250))throw Error('Invalid haptic step: duration 1..500ms, amplitudes 0..1, frequencies 40..1250Hz');
  duration+=v.milliseconds;return v;
 });if(duration>2000)throw Error('Haptic pattern exceeds 2000ms');return {side,steps:normalized};
}
async function play(api,handles,options){
 const {side,steps}=validate(options),selected=side===2?handles:handles.slice(side,side+1);if(!selected.length||selected.some(h=>!h))throw Error('Vibration device unavailable');
 if(await api.result('vibrationPermitted',[api.base+0x900])!==0||(await api.read(0x900,1))[0]===0)throw Error('System vibration disabled');
 if(api.rpc.generationGuarded===true){
  if(typeof api.rpc.haptics!=='function')throw Error('This guarded channel has no native haptic executor');
  const values=[];for(const handle of selected)values.push((await api.read(handle-api.base,4)).readUInt32LE());
  const receipt=await api.rpc.haptics({handles:values,steps});if(receipt.result!==0n)throw Error('Native haptic effect failed: '+receipt.nativeResult);
  return {result:0,actualResult:0,actual:receipt.actual,afterStop:receipt.afterStop};
 }
 const value=Buffer.alloc(16);let result,worker;
 try{
  // Compile the entire bounded envelope, including stop commands, into one native job.
  api.hapticWorker??=await require('../native/finite.cjs').create(api.rpc);worker=api.hapticWorker;
  const code=require('../abi/thread-code.json').functions,calls=[],checks=[];
  const send=(handle,address)=>{checks.push(calls.length);calls.push({function:api.functions.vibrationSend,args:[handle,address]});};
  for(let i=0;i<steps.length;i++){
   const step=steps[i],offset=0x1000+i*16;
   [step.lowAmplitude,step.lowFrequency,step.highAmplitude,step.highFrequency].forEach((v,j)=>value.writeFloatLE(v,j*4));await api.write(offset,value);
   for(let remaining=step.milliseconds;remaining>0;remaining-=5){
    for(const handle of selected)send(handle,api.base+offset);
    checks.push(calls.length);calls.push({function:api.functions.vibrationRead,args:[api.base+0xa00,selected[0]]});
    calls.push({function:api.rpc.a.sdk+code.sleep.offset,args:[Math.min(5,remaining)*1000000]});
   }
  }
  const silence=Buffer.alloc(16);silence.writeFloatLE(160,4);silence.writeFloatLE(320,12);await api.write(0x1300,silence);
  for(const handle of selected)send(handle,api.base+0x1300);
  const results=await worker.run(calls);if(checks.some(i=>results[i]!==0n))throw Error('Native haptic send/read failed');
  const streamedActual=await api.read(0xa00,16);
  const actualResult=await api.result('vibrationRead',[api.base+0xa00,selected[0]]);if(actualResult)throw Error('Vibration read Result '+actualResult);
  result={result:0,actualResult,actual:streamedActual,afterStop:await api.read(0xa00,16)};
 }finally{
  value.writeFloatLE(0,0);value.writeFloatLE(0,8);await api.write(0xb00,value);let failure;
  for(const handle of selected)try{const r=await api.result('vibrationSend',[handle,api.base+0xb00]);if(r)throw Error('Vibration stop Result '+r);}catch(error){failure??=error;}
  if(failure)throw failure;
 }
 return result;
}
module.exports={validate,play};
