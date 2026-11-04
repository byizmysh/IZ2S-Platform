// FTP state machine. Transport and storage are injected for console operation/tests.
const {virtualPath,nativePath,CHUNK}=require('./ftp-native.cjs');
function clean(s){return String(s).replace(/[\r\n\x00]/g,' ').slice(0,180);}
function create({transport:t,storage:fs,now=Date.now,readOnly=false,greeting='ftpd-sb ready. /save and /tmp read-write; /rom read-only.'}){
 let listener=null,client=null,busy=false;
 const state={running:false,address:'0.0.0.0',port:0,clients:0,transferring:false,received:0,sent:0,files:0,error:'',activity:'Stopped'};
 function reply(code,message){if(!client)return;const b=Buffer.from(code+' '+clean(message)+'\r\n');client.output=Buffer.concat([client.output,b]);if(client.output.length>65536)throw Error('Control output overflow');}
 async function dataClose(){if(!client)return;const x=client.transfer;client.transfer=null;state.transferring=false;let error;try{if(x?.file)await fs.close(x.file);}catch(e){error=e;}for(const fd of [client.data?.fd,client.passive?.fd])if(fd!==undefined)try{await t.close(fd);}catch(e){error??=e;}client.data=null;client.passive=null;if(error)throw error;}
 async function disconnect(){if(!client)return;const fd=client.fd;try{await dataClose();}finally{client=null;state.clients=0;await t.close(fd);}}
 async function passive(extended){await dataClose();client.passive=await t.listen(0);client.dataSince=now();const port=client.passive.port;if(extended)reply(229,'Entering Extended Passive Mode (|||'+port+'|)');else reply(227,'Entering Passive Mode ('+state.address.replaceAll('.',',')+','+(port>>8)+','+(port&255)+')');}
 async function transfer(kind,arg){if(!client.passive&&!client.data){reply(425,'Use PASV or EPSV first');return;}if(client.transfer){reply(450,'Transfer already active');return;}const value=virtualPath(client.cwd,arg||'.');let x={kind,path:value,offset:0,pending:Buffer.alloc(0),started:now()};
  if(kind==='RETR'){x.file=await fs.openRead(value);x.offset=client.restart;client.restart=0;if(x.offset>x.file.size){await fs.close(x.file);throw Error('Restart offset exceeds file size');}}
  else if(kind==='STOR'||kind==='APPE'){nativePath(value,true);if(client.restart){client.restart=0;throw Error('Upload restart unsupported; use APPE or upload again');}}
  else{const entries=await fs.list(value);x.pending=Buffer.from(entries.map(e=>kind==='NLST'?e.name:kind==='MLSD'?`type=${e.type==='dir'?'dir':'file'};size=${e.size}; ${e.name}`:`${readOnly?(e.type==='dir'?'dr-xr-xr-x':'-r--r--r--'):(e.type==='dir'?'drwxr-xr-x':'-rw-r--r--')} 1 ftp ftp ${String(e.size).padStart(12)} Jan 01  1970 ${e.name}`).join('\r\n')+(entries.length?'\r\n':''));}
  client.transfer=x;state.transferring=true;state.activity=kind+' '+value;reply(150,'Opening binary data connection');
 }
 async function command(line){const split=line.indexOf(' '),verb=(split<0?line:line.slice(0,split)).toUpperCase(),arg=split<0?'':line.slice(split+1);if(!/^[A-Z]{3,5}$/.test(verb)){reply(500,'Invalid command');return;}
  if(verb==='QUIT'){await dataClose();reply(221,'Goodbye');client.quitting=true;return;}
  if(verb==='USER'){client.logged=false;client.user=arg;reply(331,'Send password');return;}
  if(verb==='PASS'){client.logged=client.user==='anonymous'||client.user==='ftp';reply(client.logged?230:530,client.logged?'Logged in':'Use anonymous login');return;}
  if(verb==='FEAT'){client.output=Buffer.concat([client.output,Buffer.from('211-Features\r\n UTF8\r\n EPSV\r\n PASV\r\n SIZE\r\n REST STREAM\r\n MLST type*;size*;\r\n211 End\r\n')]);return;}
  if(verb==='SYST'){reply(215,'UNIX Type: L8');return;}
  if(verb==='OPTS'){reply(/^UTF8 ON$/i.test(arg)?200:501,'UTF8 ON supported');return;}
  if(verb==='NOOP'){reply(200,'OK');return;}
  if(!client.logged){reply(530,'Log in first');return;}
  if(readOnly&&['STOR','APPE','DELE','RMD','MKD','RNFR','RNTO'].includes(verb)){reply(550,'Read-only filesystem export');return;}
  if(verb==='ABOR'){const active=!!client.transfer;await dataClose();if(active)reply(426,'Transfer aborted; partial uploads may remain');reply(226,'Abort complete');return;}
  if(client.transfer){reply(450,'Transfer in progress');return;}
  if(verb==='PWD'||verb==='XPWD'){reply(257,'"'+client.cwd.replaceAll('"','""')+'"');return;}
  if(verb==='CWD'||verb==='CDUP'){const p=virtualPath(client.cwd,verb==='CDUP'?'..':arg);if((await fs.stat(p)).type!=='dir')throw Error('Not a directory');client.cwd=p;reply(250,'Directory changed');return;}
  if(verb==='TYPE'){reply(['I','A','A N'].includes(arg.toUpperCase())?200:504,'Transfers preserve file bytes');return;}
  if(verb==='MODE'||verb==='STRU'){reply((verb==='MODE'?arg==='S':arg==='F')?200:504,'Stream/file mode only');return;}
  if(verb==='PASV'||verb==='EPSV'){if(verb==='EPSV'&&arg&&arg!=='1'){reply(522,'IPv4 only');return;}await passive(verb==='EPSV');return;}
  if(verb==='PORT'||verb==='EPRT'){reply(502,'Use passive mode');return;}
  if(['LIST','NLST','MLSD','RETR','STOR','APPE'].includes(verb)){await transfer(verb,/^-(a|l|la|al)$/.test(arg)&&verb==='LIST'?'.':arg);return;}
  if(verb==='REST'){if(!/^\d+$/.test(arg)||!Number.isSafeInteger(Number(arg))){reply(501,'Invalid offset');return;}client.restart=Number(arg);reply(350,'Restart offset accepted for RETR');return;}
  if(verb==='SIZE'){const p=virtualPath(client.cwd,arg),s=await fs.stat(p);if(s.type!=='file')throw Error('Not a file');reply(213,s.size);return;}
  if(verb==='DELE'||verb==='RMD'){await fs.remove(virtualPath(client.cwd,arg),verb==='RMD');reply(250,'Removed and committed');return;}
  if(verb==='MKD'){const p=virtualPath(client.cwd,arg);await fs.mkdir(p);reply(257,'"'+p.replaceAll('"','""')+'" created');return;}
  if(verb==='RNFR'){const p=virtualPath(client.cwd,arg);nativePath(p,true);await fs.stat(p);client.rename=p;reply(350,'Send RNTO');return;}
  if(verb==='RNTO'){if(!client.rename){reply(503,'RNFR required');return;}const from=client.rename;client.rename=null;await fs.rename(from,virtualPath(client.cwd,arg));reply(250,'Renamed and committed');return;}
  reply(502,'Command not implemented');
 }
 async function advance(){const x=client.transfer;if(!x)return;if(now()-x.started>30000){await dataClose();reply(426,'Data connection timed out; partial uploads may remain');return;}if(!client.data)return;
  if(x.kind==='STOR'||x.kind==='APPE'){
   if(!x.file){x.file=await fs.openWrite(x.path,x.kind==='APPE');x.offset=x.file.size;}
   const bytes=await t.recv(client.data.fd);if(bytes===null)return;if(bytes.length){await fs.write(x.file,x.offset,bytes);x.offset+=bytes.length;state.received+=bytes.length;client.last=x.started=now();return;}
  }else{
   if(!x.pending.length&&x.kind==='RETR'&&x.offset<x.file.size)x.pending=await fs.read(x.file,x.offset,CHUNK);
   if(x.pending.length){const n=await t.send(client.data.fd,x.pending);if(n<0||n>x.pending.length)throw Error('Invalid send count');x.pending=x.pending.subarray(n);x.offset+=n;state.sent+=n;if(n)client.last=x.started=now();return;}
  }
  await dataClose();state.files++;state.activity='Transfer complete';reply(226,'Transfer complete; writes committed');
 }
 return {state,async start(port=5000){if(state.running)return;if(!Number.isInteger(port)||port<1024||port>65535)throw Error('Port must be 1024..65535');state.address=await t.address();listener=await t.listen(port);state.port=listener.port;state.running=true;state.error='';state.activity='Listening';},async stop(){try{await disconnect();}finally{if(listener){await t.close(listener.fd);listener=null;}state.running=false;state.activity='Stopped';}},async pump(){if(!state.running||busy)return;busy=true;try{
  if(!client){const accepted=await t.accept(listener.fd);if(!accepted)return;client={...accepted,cwd:'/',logged:false,user:'',restart:0,input:Buffer.alloc(0),output:Buffer.alloc(0),last:now(),passive:null,data:null,transfer:null};state.clients=1;reply(220,greeting);}
  if(now()-client.last>120000){await disconnect();return;}
  if(client.output.length){const n=await t.send(client.fd,client.output);client.output=client.output.subarray(n);if(client.output.length)return;}
  if(client.quitting){await disconnect();return;}
  const bytes=await t.recv(client.fd,4096);if(bytes!==null){if(!bytes.length){await disconnect();return;}client.last=now();client.input=Buffer.concat([client.input,bytes]);if(client.input.length>8192)throw Error('Command too long');}
  const end=client.input.indexOf(10);if(end>=0){const line=client.input.subarray(0,end).toString('utf8').replace(/\r$/,'');client.input=client.input.subarray(end+1);try{await command(line);}catch(e){state.error=clean(e.message);reply(550,state.error);}}
  if(client.passive&&!client.data){const data=await t.accept(client.passive.fd);if(data){if(data.peer!==client.peer)await t.close(data.fd);else{client.data=data;await t.close(client.passive.fd);client.passive=null;}}else if(now()-client.dataSince>30000){await dataClose();reply(425,'Passive connection timed out');}}
  if(client.transfer)try{await advance();}catch(e){state.error=clean(e.message);try{await dataClose();}catch{}reply(451,state.error+'; partial uploads may remain');}
 }catch(e){state.error=clean(e.message);try{await disconnect();}catch{}state.activity='Client disconnected';}finally{busy=false;}}};
}
module.exports={create};
