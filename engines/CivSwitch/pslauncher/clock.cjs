// An atlas mailbox keeps wall-clock time separate from menu/controller state.
function header(date=new Date()){
 const b=Buffer.alloc(16);b.writeUInt32LE(0x5053544d);
 const parts=new Intl.DateTimeFormat('en-GB',{hour:'2-digit',minute:'2-digit',hourCycle:'h23'}).formatToParts(date);
 b.writeUInt32LE(Number(parts.find(p=>p.type==='hour').value),4);b.writeUInt32LE(Number(parts.find(p=>p.type==='minute').value),8);return b;
}
module.exports={header};
