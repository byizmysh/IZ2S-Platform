'use strict';

const assert = require('node:assert/strict');
const {createHash, createPrivateKey, createPublicKey, sign, verify} = require('node:crypto');
const fs = require('node:fs/promises');
const {decode} = require('../../../core/console/sd-launcher/package.cjs');
const domain = Buffer.from('IZ2S-SD-CAPABILITY-v1\0iz-file-deck\0whole-sd\0');

function signedMessage(image){
  assert.equal(decode(image).id,'iz-file-deck','Expected an Foldrift image.');
  return Buffer.concat([domain,createHash('sha256').update(image).digest()]);
}

async function main([operation,imageFile,keyFile,signatureFile,...extra]){
  assert(['sign','verify'].includes(operation)&&imageFile&&keyFile&&signatureFile&&!extra.length,
    'Usage: node sign-deck.cjs sign|verify Deck.iza publisher.pem launcher.iza.sig');
  const message=signedMessage(await fs.readFile(imageFile));
  if(operation==='sign'){
    const key=createPrivateKey(await fs.readFile(keyFile));
    assert.equal(key.asymmetricKeyType,'ed25519');
    await fs.writeFile(signatureFile,sign(null,message,key),{flag:'wx',mode:0o644});
  }else{
    const key=createPublicKey(await fs.readFile(keyFile));
    assert.equal(key.asymmetricKeyType,'ed25519');
    const signature=await fs.readFile(signatureFile);
    assert(signature.length===64&&verify(null,message,key,signature),'Invalid Deck publisher signature.');
  }
}

if(require.main===module)main(process.argv.slice(2)).catch(error=>{
  console.error(error.message);
  process.exitCode=1;
});
module.exports={signedMessage};
