// Private app mailbox outside the SDK request, FTP and optical-input ranges.
const OFFSET=0x7f8000,BYTES=4096,MAGIC=0x495a4658,ROWS=12,ROW=256;
function writeText(b,offset,size,text){const value=Buffer.from(String(text),'utf8');value.copy(b,offset,0,Math.min(size-1,value.length));}
function encode({ack=0,generation=0,cwd='/',entries=[],page=0,ftp={},error='',busy=false,mode='files'}){
 const pages=Math.max(1,Math.ceil(entries.length/ROWS));page=Math.max(0,Math.min(page,pages-1));
 const b=Buffer.alloc(BYTES-64),put=(at,value)=>b.writeUInt32LE(value>>>0,at-64);
 put(64,MAGIC);put(68,ack);put(72,busy?3:error?2:1);put(76,Math.min(ROWS,Math.max(0,entries.length-page*ROWS)));
 put(80,entries.length);put(84,page);put(88,pages);put(92,generation);writeText(b,96-64,160,cwd);
 for(const [i,e]of entries.slice(page*ROWS,(page+1)*ROWS).entries()){
  const at=256+i*ROW-64;b.writeUInt32LE(e.type==='dir'?0:e.type==='game'?2:1,at);b.writeBigUInt64LE(BigInt(e.size||0),at+8);writeText(b,at+16,240,e.name);
 }
 writeText(b,3328-64,256,error||ftp.error||'');put(3584,ftp.running?ftp.transferring?2:1:0);put(3588,ftp.port||2121);put(3592,ftp.clients||0);
 const address=String(ftp.address||'0.0.0.0').split('.');for(let i=0;i<4;i++)b[3596-64+i]=Number(address[i])||0;
 b.writeBigUInt64LE(BigInt(ftp.sent||0),3600-64);b.writeBigUInt64LE(BigInt(ftp.received||0),3608-64);
 writeText(b,3616-64,192,ftp.activity||'Starting file service');
 put(3808,mode==='storage'?2:mode==='games'?1:0);
 return b;
}
module.exports={OFFSET,BYTES,MAGIC,ROWS,ROW,encode};
