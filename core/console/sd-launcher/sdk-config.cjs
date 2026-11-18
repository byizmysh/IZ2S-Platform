'use strict';
// Fixed metadata in the existing header tail. This adds no section, padding
// image or manually selected autonomy flag to an application's payload.
const assert=require('node:assert/strict'),FLAG=512,AT=400,BYTES=112,MAGIC=0x314b4453;
const CAPABILITIES=Object.freeze({udp:1,vibration:2});
function validate(value){
 if(value===undefined)return null;
 assert(value&&value.version===1&&Array.isArray(value.capabilities)&&value.capabilities.every(name=>typeof name==='string'&&Object.hasOwn(CAPABILITIES,name))&&new Set(value.capabilities).size===value.capabilities.length,'SDK services need an available native SD dispatcher');
 assert(value.services===undefined||Array.isArray(value.services)&&!value.services.length,'Native SDK background services are unavailable');assert(value.ftpPort===undefined,'Native SDK FTP is unavailable');
 const endpoints=value.endpoints??[];assert(Array.isArray(endpoints)&&endpoints.length<=8,'SDK supports at most eight UDP endpoints');const peers=endpoints.map(require('../host/network.cjs').endpoint),mask=value.capabilities.reduce((bits,name)=>bits|CAPABILITIES[name],0);assert(mask&CAPABILITIES.udp||!peers.length,'UDP endpoints require the UDP capability');return mask?{mask,endpoints:peers}:null;
}
function encode(value){const config=validate(value);if(!config)return null;const b=Buffer.alloc(BYTES);b.writeUInt32LE(MAGIC);b.writeUInt32LE(1,4);b.writeUInt32LE(config.mask,8);b.writeUInt32LE(config.endpoints.length,12);config.endpoints.forEach((peer,i)=>{const at=16+i*8,ip=peer.address.split('.').reduce((n,p)=>n*256+Number(p),0);b.writeUInt32LE(ip,at);b.writeUInt32LE(peer.port,at+4);});b.writeUInt32LE(require('./package.cjs').crc32(b.subarray(0,80)),80);return b;}
function decode(b){assert(Buffer.isBuffer(b)&&b.length===BYTES&&b.readUInt32LE()===MAGIC&&b.readUInt32LE(4)===1&&b.subarray(84).every(n=>n===0),'Invalid native SDK metadata');const mask=b.readUInt32LE(8),count=b.readUInt32LE(12);assert(mask>0&&mask<=3&&count<=8&&((mask&1)||count===0),'Invalid native SDK capabilities');assert.equal(b.readUInt32LE(80),require('./package.cjs').crc32(b.subarray(0,80)),'Native SDK metadata checksum differs');const endpoints=[];for(let i=0;i<8;i++){const ip=b.readUInt32LE(16+i*8),port=b.readUInt32LE(20+i*8);if(i>=count){assert(!ip&&!port);continue;}assert(port>0&&port<=65535);endpoints.push({address:[ip>>>24,(ip>>>16)&255,(ip>>>8)&255,ip&255].join('.'),port});}return {version:1,capabilities:Object.keys(CAPABILITIES).filter(name=>mask&CAPABILITIES[name]),endpoints};}
module.exports={FLAG,AT,BYTES,MAGIC,CAPABILITIES,validate,encode,decode};
