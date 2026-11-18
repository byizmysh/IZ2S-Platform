// Keep one upload and one next frame; spend idle ACK time before capturing,
// instead of letting an already encoded frame age in a queue.
(function(root){
 function captureDelay(uploadMs,captureMs,encodeMs,intervalMs=0){
  if(![uploadMs,captureMs,encodeMs,intervalMs].every(Number.isFinite))return 0;
  // The next frame cannot leave before either the ACK or the FPS deadline.
  // Capturing near the later deadline avoids encoding twice at fast ACK rates.
  return Math.max(0,Math.min(100,Math.max(uploadMs,intervalMs)-captureMs-encodeMs-1.5));
 }
 function interval(fps){return 1000/([30,40,60].includes(Number(fps))?Number(fps):30);}
 function staleFrame(start,now,captureMs,encodeMs){
  if(![start,now,captureMs,encodeMs].every(Number.isFinite))return true;
  const work=Math.max(0,captureMs)+Math.max(0,encodeMs);
  return now-start>work+16;
 }
 function createRates(start=0){
  let began=start,events=[];
  function reset(now){began=now;events=[];}
  function record(now,updated=true){events.push({at:now,updated});return snapshot(now);}
  function snapshot(now){events=events.filter(e=>e.at>now-1000);const elapsed=Math.min(1000,Math.max(1,now-began));return {ready:now-began>=250,captured:events.length*1000/elapsed,updated:events.filter(e=>e.updated).length*1000/elapsed};}
  return {reset,record,snapshot};
 }
 const api={captureDelay,interval,staleFrame,createRates};if(typeof module==='object')module.exports=api;else root.IZPacing=api;
})(typeof globalThis==='object'?globalThis:this);
