'use strict';
// Version-pinned persistent GPU audio lifetime over the existing finite owner.
// Does not provide/replace an AudioOut backend.
const SONIC={id:'izsonic-audio14-2',version:'0.14.2-experimental',fingerprint:'e3a7284ad1ee343a8b35132db9ac16f5fbf0ca486874a5e4f1fc3708e430ee25'};
const SMOOTH={id:'izsonic-smooth14-3',version:'0.14.3-experimental',fingerprint:'f05e3395c04551407181ba168e9dcf762c47e8499878a7208be70560a44a1514'};
const IZDOOM={id:'izdoom-native18',version:'0.18.0-experimental',fingerprint:'9bb7a335678a1f91a2c7aac4004c92930ce74335734c7ed8cd190467c92ca3ee'};
const IZDOOM_WIDE={id:'izdoom-wide18',version:'0.18.1-experimental',fingerprint:'feb74a867d14eb85a32fee27d170cf8946ae153d51ee8efc244a8261981b7be0'};
const IZDOOM_PRIME={id:'izdoom-prime18',version:'0.18.2-experimental',fingerprint:'32fe9ee18df9e860cf13fb58e0d7b9674a447b48b233dc58f1b03600cddd4987'};
function doomCandidate(m){return [IZDOOM,IZDOOM_WIDE,IZDOOM_PRIME].some(s=>m.id===s.id&&m.version===s.version&&m.hostRuntime==='module:'+s.id+'/player')&&m.memory?.bytes===68*1024*1024&&m.memory.profile==='resident-audio-128'&&m.computeSequence?.repeat===2&&String(m.computeWorkgroups||[1,1,1])==='1,1,1';}
function candidate(m){if(doomCandidate(m))return true;return [SONIC,SMOOTH].some(s=>m.id===s.id&&m.version===s.version&&m.hostRuntime==='module:'+s.id+'/player')&&
 m.memory?.bytes===76*1024*1024&&m.memory.profile==='resident-audio-128'&&m.computeSequence?.repeat===1&&String(m.computeWorkgroups)==='40,28,1';}
function policy(app){
 if([IZDOOM.id,IZDOOM_WIDE.id,IZDOOM_PRIME.id].includes(app.id)){
  if(![IZDOOM,IZDOOM_WIDE,IZDOOM_PRIME].some(s=>app.id===s.id&&app.version===s.version&&app.fingerprint===s.fingerprint))throw Error('Finite audio qualification requires the pinned IZDOOM image');
  if(!doomCandidate(app.manifest)||app.files.memory?.allocationBytes!==68*1024*1024)throw Error('IZDOOM audio geometry differs');
  const s=app.files.state.bytes;
  if(s.readUInt32LE(784)!==0x44413138||s.readUInt32LE(956)!==0x44313847)throw Error('IZDOOM native audio ABI differs');
  return {service:1,privateResetBytes:32*1024*1024,privateKeepFirst:0,privateKeepEnd:0,keepControlFirst:784,keepControlEnd:960,wallClock:true};
 }

 if(![SONIC,SMOOTH].some(s=>app.id===s.id&&app.version===s.version&&app.fingerprint===s.fingerprint))throw Error('Finite audio qualification requires a pinned Sonic image');
 if(!candidate(app.manifest)||app.files.memory?.allocationBytes!==76*1024*1024)throw Error('Sonic finite audio geometry differs');
 const state=app.files.state.bytes;
 if(state.readUInt32LE(784)!==0x53413134||state.readUInt32LE(784+43*4)!==0x41313447)throw Error('Sonic finite audio control ABI differs');
 // GPU-decoded effect cache is inside the VM allocation, not in the bank tail.
 // Its exact range comes from the pinned 78-track table in the initial image.
 return {service:1,privateResetBytes:32*1024*1024,privateKeepFirst:8388608,privateKeepEnd:16163008,
  keepControlFirst:784,keepControlEnd:960,wallClock:true};
}
module.exports={policy,candidate,SONIC,SMOOTH,IZDOOM,IZDOOM_WIDE,IZDOOM_PRIME};
