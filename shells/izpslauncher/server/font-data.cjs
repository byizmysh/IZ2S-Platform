// The catalog ABI keeps its existing offset; only the packed font is stored on disk.
const fs=require('node:fs/promises'),path=require('node:path'),{inflateSync}=require('node:zlib');
async function load(){
 const metadata=JSON.parse(await fs.readFile(path.join(__dirname,'shaders/launcher-text.json')));
 const packed=inflateSync(await fs.readFile(path.join(__dirname,'shaders/launcher-text.deflate')),{maxOutputLength:65536});
 if(metadata.bits!==2||metadata.width!==512||packed.length!==metadata.bytes||packed.length%4)
  throw Error('Invalid IZ font atlas');
 const data=Buffer.alloc(1024*2048);(await require('../../../language/native-atlas.cjs').localize(packed,{kind:'legacy',shell:'izpslauncher',language:'en'})).copy(data);return data;
}
module.exports={load};
