// One request waits for a new compositor paint. No frame queue and no stale
// image relabeled as a fresh capture while input or the output pipe is busy.
function create({invalidate,write,onWait=()=>{},onError=()=>{},beforeCapture=async()=>{},now=Date.now,waitMs=500}){
 let revision=0,latest,pending,sending=false,closed=false;
 const report={paints:0,frames:0,busyRequests:0,waits:0};
 function waiting(reason){if(!pending)return;const own=pending;pending=null;clearTimeout(own.timer);report.waits++;onWait({requestId:own.id,reason});}
 async function flush(){
  if(closed||sending||!pending?.armed||!latest||latest.revision<=pending.after)return;
  const own=pending,frame=latest;pending=null;clearTimeout(own.timer);sending=true;
  try{await write({...frame,requestId:own.id});report.frames++;report.lastPaintAgeMs=Math.max(0,now()-frame.at);}
  catch(error){onError(error);}finally{sending=false;}
 }
 return {report,
  paint(image){if(closed)return;latest={image,at:now(),revision:++revision};report.paints++;
   if(pending?.armed&&revision>pending.after&&pending.discardPaints>0){pending.discardPaints--;pending.after=revision;report.discardedPaints=(report.discardedPaints||0)+1;invalidate();return;}
   flush();
  },
  inputChanged(){if(closed||!pending)return;pending.after=revision;if(pending.armed)invalidate();},
  async request(id=0){
   if(closed)return false;
   if(pending||sending){report.busyRequests++;onWait({requestId:id,reason:'capture busy'});return false;}
   const own={id,after:revision,armed:false};pending=own;own.timer=setTimeout(()=>waiting('waiting for a compositor paint'),waitMs);
   try{const policy=await beforeCapture();if(closed||pending!==own)return false;own.after=revision;own.discardPaints=policy?.discardPaints===1?1:0;own.armed=true;invalidate();return true;}
   catch(error){if(pending===own)waiting(error.message);onError(error);return false;}
  },
  reset(){waiting('render profile changed');latest=null;},
  close(){closed=true;if(pending)clearTimeout(pending.timer);pending=null;latest=null;}
 };
}
module.exports={create};
