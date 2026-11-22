'use strict';
const net=require('node:net');
// FTP greeting, not merely an open socket. File identity is checked after login.
function probe(host,{timeout=1000,signal}={}) {
  if(signal?.aborted)return Promise.resolve(false);
  return new Promise(resolve=>{
    let socket,done=false,text='';
    const finish=ok=>{if(done)return;done=true;clearTimeout(timer);signal?.removeEventListener('abort',abort);socket?.destroy();resolve(ok);};
    const abort=()=>finish(false),timer=setTimeout(abort,timeout);
    signal?.addEventListener('abort',abort,{once:true});
    socket=net.createConnection({host,port:1425});
    socket.on('data',chunk=>{text+=chunk;if(/^220[ -]/m.test(text))finish(true);else if(text.length>2048)finish(false);});
    socket.on('error',abort);socket.on('close',abort);
  });
}
module.exports={probe};
