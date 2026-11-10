const {scan}=require('../descriptors.cjs');
async function verifyOriginals(root){const engines=scan(root,'engine');if(engines.errors.length)throw Error(engines.errors.map(e=>e.id+': '+e.error).join('; '));return {files:engines.items.length,engines:engines.items.map(e=>({id:e.id,bootstrap:e.bootstrap?.sha256||null}))};}
module.exports={verifyOriginals};
