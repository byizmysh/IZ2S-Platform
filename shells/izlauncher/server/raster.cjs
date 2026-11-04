const fs=require('node:fs/promises'),path=require('node:path'),{execFile}=require('node:child_process'),{promisify}=require('node:util');
async function raster(catalog,directory,label='focus-catalog'){
 await require('../../../core/services/app-catalog.cjs').raster(catalog,directory,label);
 const input=path.join(directory,label+'.json'),output=path.join(directory,label+'.bin');
 await promisify(execFile)(require('../../../core/platform/python.cjs').command(),[path.join(__dirname,'hero-text.py'),input,output],{timeout:15000,maxBuffer:65536});return fs.readFile(output);
}
module.exports={raster};
