'use strict';
const net=require('node:net'),dgram=require('node:dgram'),crypto=require('node:crypto');
function localAddress(host){return new Promise((resolve,reject)=>{const socket=dgram.createSocket('udp4');socket.once('error',error=>{socket.close();reject(error);});socket.connect(9026,host,()=>{const address=socket.address().address;socket.close();resolve(address);});});}
async function write(options,request,address,bytes){
 const data=Buffer.from(bytes);if(!data.length||data.length>128*1024*1024)throw Error('Invalid native stream size');
 const host=await localAddress(options.host),token=crypto.randomBytes(32).toString('hex'),sha256=crypto.createHash('sha256').update(data).digest('hex');
 let peer,authenticated=false;const server=net.createServer(socket=>{
  if(peer||socket.remoteAddress!==options.host){socket.destroy();return;}peer=socket;socket.setTimeout(10000,()=>socket.destroy());
  let handshake='';socket.on('error',()=>{});socket.on('data',chunk=>{
   if(authenticated){socket.destroy();return;}
   handshake+=chunk.toString('ascii');if(handshake.length>65){socket.destroy();return;}
   if(handshake.endsWith('\n')){if(handshake!==token+'\n'){socket.destroy();return;}authenticated=true;socket.end(data);}
  });
 });
 await new Promise((resolve,reject)=>{server.once('error',reject);server.listen(0,host,resolve);});
 try{
  const result=await request({address:String(address),size:data.length,host,port:server.address().port,token,sha256});
  if(!authenticated||result.written!==data.length||result.sha256!==sha256)throw Error('Native stream was not fully confirmed');return result;
 }finally{peer?.destroy();await new Promise(resolve=>server.close(resolve));}
}
module.exports={write,localAddress};
