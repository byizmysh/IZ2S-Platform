'use strict';
// Fixed original Switch player IDs: no host discovery is needed after startup.
const L=require('./layout.cjs'),HEADER=L.FIELD.padSweepHeader,OFFSET=L.FIELD.padSweep,COUNT=32,MAGIC=0x495a5044;
const STYLES=['FullKey','JoyDual','JoyLeft','JoyRight'];
function header(){const b=Buffer.alloc(16);b.writeUInt32LE(MAGIC);b.writeUInt32LE(1,4);b.writeUInt32LE(COUNT,8);return b;}
function frameCalls(functions,base,extension){return Array.from({length:8},(_,id)=>STYLES.map((kind,i)=>({function:functions['pad'+kind],args:[extension+OFFSET+(id*4+i)*64,base+0x1020+id*4]}))).flat();}
module.exports={HEADER,OFFSET,COUNT,MAGIC,STYLES,header,frameCalls};
