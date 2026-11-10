'use strict';
const {EventEmitter}=require('node:events'),assert=require('node:assert/strict');
// Preserve the parent session's event source while replacing a healthy legacy
// route. Handover after a failed legacy operation is intentionally refused.
class SwitchableConnection extends EventEmitter{
 constructor(connection){super();this.socket=this;this.delegate=connection;this.inflight=0;this.awaitingResponse=false;this.stopped=false;this.watch(connection);}
 watch(connection){this.closedHandler=()=>{if(this.delegate===connection&&!this.stopped){this.stopped=true;this.error=connection.error||Error('Console connection closed');this.emit('close');}};connection.socket.once('close',this.closedHandler);}
 get suspended(){return !!this.delegate.suspended;}
 get destroyed(){return this.stopped;}
 get remoteAddress(){return this.delegate.socket.remoteAddress;}
 get timeout(){return this.delegate.timeout;}set timeout(value){this.delegate.timeout=value;}
 async write(bytes){assert(!this.stopped,'Connection closed');assert(!this.awaitingResponse,'Previous native response is still pending');this.inflight++;if(bytes.subarray(0,80).some(v=>v!==0))this.awaitingResponse=true;try{return await this.delegate.write(bytes);}finally{this.inflight--;}}
 async read(size){assert(!this.stopped&&this.awaitingResponse,'No native response is pending');this.inflight++;try{const bytes=await this.delegate.read(size);this.awaitingResponse=false;return bytes;}finally{this.inflight--;}}
 handover(replacement){assert(!this.stopped&&!this.inflight&&!this.awaitingResponse&&!this.delegate.error,'The legacy native command boundary is not quiescent');assert(replacement&&!replacement.error&&!replacement.suspended,'Replacement native transport has not synchronized');const old=this.delegate;old.socket.off('close',this.closedHandler);this.delegate=replacement;this.watch(replacement);old.close();return old;}
 fail(error){if(this.stopped)return;this.error=error;this.delegate.fail(error);}
 close(){if(this.stopped)return;this.stopped=true;this.delegate.socket.off('close',this.closedHandler);this.delegate.close();this.emit('close');}
}
module.exports={SwitchableConnection};
