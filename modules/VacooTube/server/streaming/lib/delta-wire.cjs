// Experimental bounded wire look-ahead. A written packet cannot be cancelled or
// replaced: reset closes the stream and the native receiver must join first.
const {deadline:defaultDeadline}=require('./ack-wait.cjs');
const protocolError=message=>Object.assign(Error(message),{code:'FRAME_PIPELINE_INVALIDATED',recoverable:true,wireProtocolFailure:true});
function create({socket,acknowledgementBytes=396,onInput=()=>{},onPause=()=>{},onError=()=>{},deadline=defaultDeadline}){
 if(!socket||![396,436].includes(acknowledgementBytes))throw Error('Invalid delta wire configuration');
 let failed=null,received=Buffer.alloc(0),closed=false;const rows=new Map(),pending=[];
 function fail(error){if(failed)return;failed=error;for(const row of rows.values()){row.deadline?.clear();row.reject(error);}pending.length=0;received=Buffer.alloc(0);socket.destroy();onError(error);}
 function onData(bytes){
  if(failed||closed)return;received=Buffer.concat([received,bytes]);
  if(received.length>acknowledgementBytes*2){fail(protocolError('Oversized delta acknowledgement batch'));return;}
  while(received.length>=acknowledgementBytes){
   const packet=Buffer.from(received.subarray(0,acknowledgementBytes));received=received.subarray(acknowledgementBytes);const row=pending.shift(),ack=Number(packet.readBigUInt64LE());
   if(!row||ack!==row.ticket.expected){fail(protocolError('Delta wire acknowledgement order differs'));return;}
   row.deadline.clear();row.acknowledged=true;row.result={ack,packet,transferAckMs:performance.now()-row.started};
   try{onInput(packet,row.ticket);}catch(error){fail(error);return;}row.resolve(row.result);
  }
 }
 socket.on('data',onData);const onSocketError=error=>fail(error),onClose=()=>{if(!closed)fail(Object.assign(Error('Delta wire socket closed'),{code:'FRAME_SOCKET_CLOSED'}));};socket.on('error',onSocketError);socket.on('close',onClose);
 function write(ticket,packet){
  if(failed)throw failed;if(closed)throw Error('Delta wire is closed');
  if(!ticket||![2,3].includes(ticket.expected)||!Number.isSafeInteger(ticket.sequence)||ticket.sequence<1||!Buffer.isBuffer(packet)||packet.length<64)throw Error('Invalid delta wire frame');
  if(rows.has(ticket)||rows.size>=2)throw Error('Delta wire already owns the current and one future packet');
  const tail=[...rows.values()].at(-1);if(tail&&(ticket.sequence!==tail.ticket.sequence+1||ticket.expected===tail.ticket.expected))throw Error('Delta wire ticket does not alternate');
  let resolve,reject;const done=new Promise((r,j)=>{resolve=r;reject=j;});done.catch(()=>{});
  const row={ticket,packet,done,resolve,reject,started:performance.now(),acknowledged:false};rows.set(ticket,row);pending.push(row);
  row.deadline=deadline({onPause:()=>onPause(ticket),onTimeout:()=>fail(Object.assign(Error('Delta wire acknowledgement timeout'),{code:'FRAME_ACK_TIMEOUT'}))});
  socket.write(packet,error=>{if(error)fail(error);});return ticket;
 }
 async function consume(ticket){const row=rows.get(ticket);if(!row)throw Error('Delta wire ticket is not owned');const result=await row.done;if(failed)throw failed;if(rows.get(ticket)!==row)throw Error('Delta wire ownership changed');rows.delete(ticket);return result;}
 function close(error=Object.assign(Error('Delta wire reset requires a joined native receiver'),{code:'FRAME_PIPELINE_INVALIDATED',recoverable:true})){if(closed)return;closed=true;fail(error);socket.off('data',onData);socket.off('error',onSocketError);socket.off('close',onClose);rows.clear();}
 return {write,consume,close,fail,get count(){return rows.size;},get pending(){return pending.length;},get failed(){return failed;},owns:ticket=>rows.has(ticket)};
}
module.exports={create};
