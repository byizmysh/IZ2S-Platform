// Native allocator counters must remain exact before planning another wave.
// A changed/corrupt heap is not a clean candidate rejection.
function uncertain(message,details={}){
 return Object.assign(Error(message),{heapStateUncertain:true,resourcesRetained:true,...details});
}
function counters(bytes){
 if(!Buffer.isBuffer(bytes)||bytes.length<128)throw uncertain('Native allocation counters are incomplete');
 return Array.from({length:16},(_,index)=>{
  const value=bytes.readBigUInt64LE(index*8);
  if(value>BigInt(Number.MAX_SAFE_INTEGER))throw uncertain('Native allocation counter changed unexpectedly; restart the game before another launch',{counterIndex:index,counterValue:value.toString()});
  return Number(value);
 });
}
function assertRestored(row){
 if(row.restored!==true)throw uncertain('General-heap allocation changed; stop acquisition and restart the game before another launch',{
  heapDrift:{profile:row.profile,expected:row.baseline,observed:row.after,difference:row.after-row.baseline}});
}
module.exports={counters,assertRestored};
