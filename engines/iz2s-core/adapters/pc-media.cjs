'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const path = require('node:path');
const archive = require('../../../core/archive.cjs');
const nativeDigest = 'd074ffedbf707a9062cf1cdf48b218048d88963fcddc13d6f86f7f75fdb18cc8';

const adapter = 'sb4-pc-media-v1';
const code = 'native/sb4-pc-media.bin';
const initial = 'native/sb4-pc-media.dat';

// Qualified console assets for the supported layouts of BlitBun and VacooTube.
const runtimes = new Map([
  ['blitbun',{source:1,digests:[
    'd66d104f78b51f1c8a767686706982ef051fa900e9bc52e7a923e40b9f6a56e2',
    'ad0e0a55556773a419853317deac362de519e484656ae50099d2d61d4fbb385d',
    '6a2847a5c0f534a51bca306a9d83bbd12f6274be860200aba05c36a9109069ef',
    'b5fda04ff54d75c5e9abae4f12ae7ce48747f72c9205c31cbb232695c2e14580',
    '3e009fe7a56f2a0b1a3ac405f9fa7c0d755999ae537b110f635ab47423921f43',
    '80ac3cc9724a2a89da4947d342ec8a9a5964c0388dfe0005bc933a6557d37bfb',
  ]}],
  ['vacootube',{source:2,digests:[
    '700bad31b8aee6d5619a1f8a861f2f5595c9d469ed0c99a2e3badca7dea2112b',
    '1ce7746c7b394b6b6e19081d8432774c447a1b4fb0a22e7d4c05cfdaa1796d6c',
    '4a9ae5653ac02c01d6332c37af2005a93fd1167df13af2479d052158eecfcde5',
    'bfd5d0d6bd5aa366f25d32c6c4110039f1e763304b8608c81f2c196a71703942',
    '3f4a021779cb5d9102b6b5ae9171e552b6d71385c9a872a75008637c41c1a0bf',
  ]}],
]);

function matchesArchive(manifest,descriptor,_sha256,validatedFiles) {
  const expected=runtimes.get(descriptor.id);
  const entry=descriptor.switch?.entry;
  if(!expected||!entry||!validatedFiles||descriptor.dependencies?.length||
      manifest.id!==descriptor.id||
      manifest.hostRuntime!=='module:'+descriptor.id+'/standalone'||
      manifest.memory?.bytes!==2097152||!manifest.shaders)return false;
  const prefix=path.posix.dirname(entry)+'/';
  const paths=[manifest.initialState,manifest.data,manifest.memory.initial,
    manifest.shaders.vertex,manifest.shaders.fragment,manifest.shaders.compute];
  if(paths.some(name=>typeof name!=='string'))return false;
  const files=paths.map(name=>validatedFiles.find(file=>file.name===prefix+name));
  if(files.some(file=>!file))return false;
  const {name,version,description,icon,...settings}=manifest;
  const layout=JSON.stringify(settings,(_,value)=>value&&typeof value==='object'&&!Array.isArray(value)
    ?Object.fromEntries(Object.keys(value).sort().map(key=>[key,value[key]])):value);
  const identity=[layout,...files.map(file=>archive.sha(file.bytes))];
  return expected.digests.includes(archive.sha(Buffer.from(JSON.stringify(identity))));
}

async function adapt(manifest) {
  const entry=runtimes.get(manifest.id);
  assert(entry&&manifest.hostRuntime==='module:'+manifest.id+'/standalone','Unexpected media module');
  const binary=await fs.readFile(path.join(__dirname,'pc-media','native.bin'));
  assert.equal(archive.sha(binary),nativeDigest,'Bundled PC media adapter differs');
  const context=Buffer.alloc(4);
  context.writeUInt32LE(entry.source);
  const portable={...manifest,sdOnly:true,native:{format:'iz2s.native-app',abi:7,
    code,initial,dataBytes:4096,capabilities:['audio-out']}};
  delete portable.hostRuntime;
  return [
    {name:'app.json',bytes:Buffer.from(JSON.stringify(portable,null,2)+'\n')},
    {name:code,bytes:binary},
    {name:initial,bytes:context},
  ];
}

async function prepareSource(directory) {
  const manifest=JSON.parse(await fs.readFile(path.join(directory,'app.json')));
  const files=await adapt(manifest);
  await fs.mkdir(path.join(directory,'native'),{recursive:true});
  for(const file of files)
    await fs.writeFile(path.join(directory,file.name),file.bytes,{flag:file.name==='app.json'?'w':'wx'});
}

async function convertArchive(checked) {
  const {entry,prefix,manifest}=archive.switchApp(checked);
  const descriptor=JSON.parse(checked.files.find(file=>file.name==='module.json').bytes);
  assert(matchesArchive(manifest,descriptor,checked.sha256,checked.files),'Media archive source differs');
  descriptor.switch.files.push(prefix+code,prefix+initial);
  const files=checked.files.filter(file=>!['module.json','checksums.json',entry].includes(file.name));
  files.push({name:'module.json',bytes:Buffer.from(JSON.stringify(descriptor,null,2)+'\n')});
  for(const file of await adapt(manifest))
    files.push({name:file.name==='app.json'?entry:prefix+file.name,bytes:file.bytes});
  const checksums={algorithm:'sha256',files:Object.fromEntries(files.map(file=>[file.name,archive.sha(file.bytes)]))};
  files.push({name:'checksums.json',bytes:Buffer.from(JSON.stringify(checksums,null,2)+'\n')});
  const bytes=archive.encode(files);
  return {bytes,checked:archive.inspect(bytes)};
}

module.exports={adapter,matchesArchive,prepareSource,convertArchive};
