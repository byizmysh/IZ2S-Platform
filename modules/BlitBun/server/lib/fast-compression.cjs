// Optional acceleration stays inside the streaming module. Missing toolchains
// or native libraries retain the exact JavaScript/zlib implementation.
const path=require('node:path');
async function prepare({root=require('./platform.cjs').root,build=require(path.join(require('./platform.cjs').root,'core/build-native.cjs')).build}={}){
 try{return {path:await build(root,'blitbun',path.join(__dirname,'../native')),backend:'libdeflate'};}
 catch(error){return {path:null,backend:'zlib',reason:error.message};}
}
function load(file){return file?require(file):null;}
module.exports={prepare,load};
