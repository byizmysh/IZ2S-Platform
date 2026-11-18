'use strict';
const fs=require('node:fs/promises'),path=require('node:path');
async function shader(directory){
 const bytes=await fs.readFile(path.join(__dirname,'pad-normalize.comp.spv'));
 // Preserve the preparation API: callers also consume the staged shader file.
 if(directory)await fs.writeFile(path.join(directory,'pad-normalize.comp.spv'),bytes);
 return bytes;
}
module.exports={shader};
