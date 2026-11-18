// Preserve the existing HD path. Full HD avoids zlib back-reference reads
// from its mapped GPU output by inflating into the owned RAM block first.
// This changes memory access only; encoded pixels, dictionaries and CRC stay
// identical. Explicit overrides support physical-console A/B measurements.
function directInflateFor(width,height,policy=process.env.IZ12_STREAM_DIRECT_INFLATE){
 if(policy==='1')return true;
 if(policy==='0')return false;
 return width*height<=1280*720;
}
module.exports={directInflateFor};
function decodePolicyFor(width,height,format,mode='auto',environment=process.env){
 if(!['auto','direct','ram'].includes(mode))throw Error('Invalid rendering mode');
 const directInflate=mode==='direct'?true:mode==='ram'?false:directInflateFor(width,height,environment.IZ12_STREAM_DIRECT_INFLATE);
 return {directInflate,heapDictionary:format===1&&!directInflate&&(mode==='ram'||mode==='auto'&&environment.IZ2S_HEAP_DICTIONARY==='1'&&width===1920&&height===1080),allowAllBc1HeapDictionary:mode==='ram'};
}
module.exports.decodePolicyFor=decodePolicyFor;
