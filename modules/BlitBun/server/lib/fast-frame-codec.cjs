// Optional host acceleration. All console BC1 bytes and quality stay identical.
const path=require('node:path');
async function prepare({root=require('./platform.cjs').root,build=require(path.join(require('./platform.cjs').root,'core/build-native.cjs')).build}={}){
 try{return {path:await build(root,'blitbun',path.join(__dirname,'../native/bc1')),backend:'native SIMD / two regions'};}
 catch(error){return {path:null,backend:'WASM SIMD',reason:error.message};}
}
module.exports={prepare};
