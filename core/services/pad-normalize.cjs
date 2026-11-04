'use strict';
const path=require('node:path'),fs=require('node:fs/promises');
async function shader(directory){
 const file=path.join(directory,'pad-normalize.comp.spv');
 require('../../sdk/build.cjs').compile(path.join(__dirname,'pad-normalize.comp'),file);
 return fs.readFile(file);
}
module.exports={shader};
