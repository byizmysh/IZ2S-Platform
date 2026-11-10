// Bounded native transfer into an allocation owned by this renderer. The PC
// endpoint is authenticated per transfer; the console verifies the final SHA256.
if(window.__iz2sCivRuntime.streamRevision!==2){
 const runtime=window.__iz2sCivRuntime,previous=runtime.dispatch;
 runtime.dispatch=request=>{
  if(request.command==='cancel-write-range'){runtime.cancelRange={address:BigInt(request.address),size:request.size};return true;}
  if(request.command==='bulk-write'){
   const range=runtime.cancelRange,address=BigInt(request.address);
   if(range&&address>=range.address&&address<BigInt(range.address)+BigInt(range.size)){delete runtime.cancelRange;throw Error('Application upload stopped for Civ7 transport update');}
   const result=previous(request);runtime.bulkProgress={address:request.address,size:request.size,total:(runtime.bulkProgress?.total||0)+request.size,at:Date.now()};return result;
  }
  if(request.command==='info')return {...previous(request),bulkProgress:runtime.bulkProgress||null};
  if(request.command!=='stream-write')return previous(request);
  const {size,port,host,token,sha256}=request,address=BigInt(request.address);
  if(!Number.isSafeInteger(size)||size<1||size>128*1024*1024||!Number.isInteger(port)||port<1||port>65535||
    !/^[a-f0-9]{64}$/.test(token)||!/^[a-f0-9]{64}$/.test(sha256)||typeof host!=='string')throw Error('Invalid stream transfer');
  const octets=host.split('.').map(Number);if(octets.length!==4||octets.some(v=>!Number.isInteger(v)||v<0||v>255)||!(octets[0]===10||octets[0]===192&&octets[1]===168||octets[0]===172&&octets[1]>=16&&octets[1]<=31))throw Error('Stream endpoint must be a local IPv4 address');
  previous({command:'validate-owned',address:request.address,size});
  const native=runtime.native,symbols={socket:'_ZN2nn6socket6SocketEiii',connect:'_ZN2nn6socket7ConnectEiPKNS0_8SockAddrEj',
    recv:'_ZN2nn6socket4RecvEiPvmi',send:'_ZN2nn6socket4SendEiPKvmi',close:'_ZN2nn6socket5CloseEi',
    copy:'memcpy',option:'_ZN2nn6socket10SetSockOptEiiiPKvj',hash:'_ZN2nn6crypto18GenerateSha256HashEPvmPKvm'};
  for(const name of Object.values(symbols))native.dispatch({command:'lookup',module:'sdk',name});
  const call=(name,args)=>BigInt(native.dispatch({command:'call',module:'sdk',name:symbols[name],args:args.map(String)}));
  const buffer=malloc(256),staging=malloc(1048576);if(!buffer||!staging)throw Error('Stream metadata allocation failed');
  let fd=-1n,total=0;
  try{
   for(let offset=0;offset<256;offset+=8)write64(buffer+BigInt(offset),0n);
   write8(buffer,16n);write8(buffer+1n,2n);write8(buffer+2n,BigInt(port>>8));write8(buffer+3n,BigInt(port&255));
   octets.forEach((v,i)=>write8(buffer+4n+BigInt(i),BigInt(v)));
   write64(buffer+32n,8n); // timeval seconds, microseconds = 0
   fd=BigInt.asIntN(32,call('socket',[2,1,0]));if(fd<0n)throw Error('Native transfer socket failed');
   if(call('option',[fd,65535,0x1006,buffer+32n,16])!==0n||call('option',[fd,65535,0x1005,buffer+32n,16])!==0n)throw Error('Native socket timeout setup failed');
   if(call('connect',[fd,buffer,16])!==0n)throw Error('Native stream connect failed');
   const handshake=token+'\n';for(let i=0;i<handshake.length;i++)write8(buffer+64n+BigInt(i),BigInt(handshake.charCodeAt(i)));
   let sent=0;while(sent<handshake.length){const count=Number(BigInt.asIntN(32,call('send',[fd,buffer+64n+BigInt(sent),handshake.length-sent,0])));if(count<=0)throw Error('Native stream authentication failed');sent+=count;}
   while(total<size){const count=Number(BigInt.asIntN(32,call('recv',[fd,staging,Math.min(1048576,size-total),0])));if(count<=0||count>size-total)throw Error('Native transfer interrupted after '+total+' bytes');if(call('copy',[address+BigInt(total),staging,count])!==address+BigInt(total))throw Error('Native stream copy failed');total+=count;}
   call('hash',[buffer+160n,32,address,size]);let digest='';for(let i=0;i<32;i++)digest+=Number(read8(buffer+160n+BigInt(i))).toString(16).padStart(2,'0');
   if(digest!==sha256)throw Error('Native transfer SHA256 mismatch');return {written:total,sha256:digest};
  }finally{if(fd>=0n)call('close',[fd]);free(staging);free(buffer);}
 };
 runtime.streamRevision=2;
}
