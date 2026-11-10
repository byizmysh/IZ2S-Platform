const fs=require('node:fs/promises'),path=require('node:path'),crypto=require('node:crypto');
async function files(root){
 const folder=path.join(root,'hardware/agents/arm-input'),sums=JSON.parse(await fs.readFile(path.join(folder,'checksums.json'))),result={};
 if(sums.algorithm!=='sha256')throw Error('Unsupported input package checksum format');
 for(const [name,digest]of Object.entries(sums.files)){
  if(!/^[a-zA-Z0-9_.-]+$/.test(name)||name==='..')throw Error('Invalid input package path');
  const bytes=await fs.readFile(path.join(folder,name));
  if(crypto.createHash('sha256').update(bytes).digest('hex')!==digest)throw Error('Input package changed; rebuild packages before installation');
  result[name]=bytes.toString('base64');
 }
 result['checksums.json']=(await fs.readFile(path.join(folder,'checksums.json'))).toString('base64');
 return result;
}
async function archive(root){await files(root);return path.join(root,'packages/iz2s-arm-input.tar.gz');}
async function installer(root,profile='sb4'){
 if(!['sb4','civ7'].includes(profile))throw Error('Choose an ARM input profile');
 await files(root);
 const file=path.join(root,'packages',profile==='civ7'?'iz2s-civ7-arm-input.run':'iz2s-arm-input.run');
 const script=await fs.readFile(file,'utf8'),digest=/ARCHIVE_SHA256 = '([a-f0-9]{64})'/.exec(script)?.[1];
 const bytes=await fs.readFile(await archive(root));
 if(!digest||crypto.createHash('sha256').update(bytes).digest('hex')!==digest)throw Error('ARM installer is outdated; rebuild input packages');
 return file;
}
module.exports={files,archive,installer};
