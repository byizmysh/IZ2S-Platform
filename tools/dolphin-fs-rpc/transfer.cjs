'use strict';
// Same bounded idle allocation span as the launcher's PS2-derived SD writer.
// Leave the IPC frame, paths, hash output and spare tail outside file data.
const CHUNK_BYTES=0x70000,LEGACY_CHUNK_BYTES=32768;
const BASE64_BYTES=Math.ceil(CHUNK_BYTES/3)*4,REQUEST_BYTES=0xa0000;
function chunkBytes(storage){const n=storage?.chunkBytes;return Number.isInteger(n)&&n>0&&n<=CHUNK_BYTES?n:LEGACY_CHUNK_BYTES;}
module.exports={CHUNK_BYTES,LEGACY_CHUNK_BYTES,BASE64_BYTES,REQUEST_BYTES,chunkBytes};
