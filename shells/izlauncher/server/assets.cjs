const fs=require('node:fs/promises'),path=require('node:path'),{inflateSync}=require('node:zlib');
async function loadTypeAtlas(){
 const folder=path.resolve(__dirname,'../app/assets');
 const metadata=JSON.parse(await fs.readFile(path.join(folder,'focus-type.json')));
 const bytes=inflateSync(await fs.readFile(path.join(folder,'focus-type.deflate')),{maxOutputLength:2*1024*1024});
 if(bytes.length!==metadata.bytes||metadata.offset!==65536||bytes.length+metadata.offset>0x1c0000)throw Error('Invalid shell type atlas');
 return {metadata,bytes:await require('../../../language/native-atlas.cjs').localize(bytes,{kind:'focus',shell:'izlauncher',language:'en'})};
}
module.exports={loadTypeAtlas};
