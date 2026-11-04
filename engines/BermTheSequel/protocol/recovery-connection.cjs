const {SessionConnection:Original}=require('./session-resume.cjs');
// A silent socket is not evidence that the native operation did not execute.
// Reopen transport and ask for its receipt, preserving the operation's budget.
class SessionConnection extends Original{
 constructor({probeMs=30000,...options}={}){super(options);this.probeMs=probeMs;}
 armProbe(){
  clearTimeout(this.readTimer);const transport=this.transport,current=this.current;
  if(!transport||!current||!this.reader||this.error)return;
  this.readTimer=setTimeout(()=>{
   if(this.transport===transport&&this.current===current)this.suspend(transport,Error('Console response is silent; waiting for an authenticated reconnect'));
  },Math.max(1,Math.min(this.probeMs,current.expires-Date.now())));
 }
 read(size){const response=super.read(size);this.armProbe();return response;}
 async attach(transport,proof){
  const old=this.transport;
  if(old){
   // The listener has already verified the pairing secret and identical process
   // addresses. Validate the receipt before touching the still-open old socket.
   // The new route can reconnect before TCP notices that the old adapter died.
   if(this.error)throw this.error;
   if(old===transport)throw Error('A console transport is already attached');
   const current=this.current,{sequence,cached}=proof||{};
   if(!Number.isSafeInteger(sequence)||sequence<0||typeof cached!=='boolean'||cached!==(sequence>0))throw Error('Invalid reconnect receipt');
   if(current&&Date.now()>=current.expires)throw Error('Native request reconnect budget expired; it was not replayed');
   if(current&&sequence===current.sequence){if(!cached)throw Error('Console receipt has no saved result');}
   else if(sequence!==this.sequence)throw Error('Console receipt differs from the pending session');
   // No await between receipt validation and replacement. suspend invalidates
   // old reads by generation; Original.attach decides cached-result vs the
   // one request proven not to have executed. Its original deadline is retained.
   this.suspend(old,Error('Authenticated console reconnected through a new network adapter'));
  }
  await super.attach(transport,proof);this.armProbe();
 }
}
module.exports={SessionConnection};
