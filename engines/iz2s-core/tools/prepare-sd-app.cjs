#!/usr/bin/env node
'use strict';
const fs = require('node:fs/promises');
const path = require('node:path');
const assert = require('node:assert/strict');
const runtime = require('../runtime.cjs');
const archive = require('../../../core/archive.cjs');
const pkg = require('../../../core/console/sd-launcher/package.cjs');
const assets = require('../../../core/console/sd-launcher/assets.cjs');
const compat = require('../sd-compat.cjs');
const defaults = require('./sd-defaults.cjs');
const { BASE } = require('../../../core/module-storage.cjs');
const bundleRoot = BASE.slice(1);
// IZA stores total_bytes and section offsets as unsigned 32-bit integers.
// Admission is based on the format, not a per-release MiB quota.
const MAX_IMAGE = 0xffffffff;
const qualifiedManifest = /^sd-bundle-[a-f0-9]{64}\.json$/;
const exportPreviews = new Set([
  'pad-normalize.comp.spv',
  'sd-tile.bin', 'sd-tile.json', 'sd-tile-icon-0.png',
  'managed-sd-tile.bin', 'managed-sd-tile.json', 'managed-sd-tile-icon-0.png',
]);

async function companionSources(input,bytes,header) {
  const parent=await fs.realpath(path.dirname(input));
  const bundle=await assets.loadBundle(input,bytes,{optional:true,header});
  if(bundle)return bundle;
  if(!(header.flags&8))return [];
  // Some previously installed SD folders only have the launcher and
  // content-addressed assets. Reconstruct metadata, never program bytes.
  const files=(await fs.readdir(parent)).filter(name=>/^asset-[a-f0-9]{64}\.izdata$/.test(name));
  assert.equal(files.length,header.assetManifest.count,'The SD companion set is incomplete or ambiguous; supply its sd-bundle.json.');
  const list=[];
  for(const name of files){
    const file=path.join(parent,name),entry=await fs.lstat(file);
    assert(entry.isFile(),'SD companion must be a regular file');
    list.push({path:name,file,bytes:entry.size,sha256:name.slice(6,70)});
  }
  assets.verifyBinding(header,list);
  return assets.prepared(list);
}

async function prepareImage(input,candidate,scratch,original,legacy=pkg.decode(original),verifiedSources) {
  if(legacy.delegated){
    const [source]=verifiedSources??await companionSources(input,original,legacy);
    assert(source && source.bytes<=archive.MAX_ARCHIVE,'Invalid managed SD source archive size.');
    const file=await fs.open(source.file,'r');
    let bytes;
    try {
      const stat=await file.stat();
      assert(stat.isFile()&&stat.size===source.bytes,'Managed SD source archive size changed.');
      bytes=Buffer.allocUnsafe(source.bytes);
      for(let offset=0;offset<bytes.length;){
        const {bytesRead}=await file.read(bytes,offset,bytes.length-offset,offset);
        assert(bytesRead>0,'Managed SD source archive is incomplete.');
        offset+=bytesRead;
      }
      assert.equal((await file.stat()).size,source.bytes,'Managed SD source archive size changed.');
    } finally { await file.close(); }
    assert.equal(archive.sha(bytes),legacy.source.sha256,'Managed SD source archive checksum differs.');
    const checked=archive.inspect(bytes);
    assert.equal(checked.descriptor.id,legacy.id,'Managed SD source archive application ID differs.');
    const result=await prepareArchive(checked,candidate,scratch,{includeLibrary:false});
    return {...result,preserved:false,sourceSha256:archive.sha(original),archiveSha256:checked.sha256};
  }
  const converted=await compat.convertImage(original,legacy,scratch);
  const bytes=converted?.bytes??original;
  const header=converted?pkg.decode(bytes):legacy, policy=compat.packed(header);
  assert(policy.compatible,policy.reason);
  const sources=converted?.sources??verifiedSources??await companionSources(input,bytes,header);
  const folder=path.join(candidate,bundleRoot,header.id);
  await fs.mkdir(folder,{recursive:true});
  const staged=await assets.stage(sources,folder), sha256=archive.sha(bytes);
  await fs.writeFile(path.join(folder,'launcher.iza'),bytes,{flag:'wx'});
  await assets.bundle(folder,{id:header.id,bytes:bytes.length,sha256,assets:staged});
  for (const file of defaults.files(header.id))
    await fs.writeFile(path.join(folder,file.name),file.bytes,{flag:'wx'});
  return {folder,id:header.id,abi:header.nativeAbi,bytes:bytes.length,sha256,assets:staged.length,
    preserved:!converted,...(converted?{adapter:converted.adapter,sourceSha256:archive.sha(original)}:{})};
}

async function prepareArchive(checked,candidate,extracted,{includeLibrary}={}) {
  const {descriptor,sha256}=checked;
  const app=archive.switchApp(checked);
  const {entry,manifest,files}=app;
  const folder=path.join(candidate,bundleRoot,descriptor.id);
  const direct = compat.directImage(descriptor, checked.files);
  if (direct) {
    await archive.writeFiles(extracted, direct.companions.map(row => ({name:row.path,bytes:row.contents})));
    const sources = direct.companions.map(({path:name,bytes,sha256}) =>
      ({path:name,bytes,sha256,file:path.join(extracted,name)}));
    const result = await prepareImage(null, candidate, extracted, direct.bytes, direct.header, sources);
    return {...result, version:descriptor.version, sourceSha256:sha256};
  }
  const program=await require('./nro-package.cjs').moduleProgram(checked,app);
  if(program){
    await fs.mkdir(folder,{recursive:true});
    await fs.writeFile(path.join(folder,'launcher.nro'),program,{flag:'wx'});
    return {id:descriptor.id,folder,format:'.nro',version:descriptor.version,
      bytes:program.length,sha256:archive.sha(program),sourceSha256:sha256,assets:0};
  }
  const policy=compat.eligibility(manifest,descriptor,files,{sha256,archiveFiles:checked.files});
  assert(policy.compatible,policy.reason);
  await archive.writeFiles(extracted,files);
  const scratch=path.join(candidate,'.build');
  const result=await compat.buildApp(extracted,scratch,policy,{entry:path.posix.basename(entry),includeLibrary});
  await fs.mkdir(folder,{recursive:true});
  for(const file of ['launcher.iza','sd-bundle.json',...result.assets.map(asset=>asset.path)])
    await fs.rename(path.join(scratch,file),path.join(folder,file));
  await fs.rm(scratch,{recursive:true,force:true});
  return {folder,id:result.id,abi:pkg.decode(result.bytes).nativeAbi,bytes:result.bytes.length,
    sha256:result.sha256,assets:result.assets.length,version:descriptor.version,sourceSha256:sha256,
    ...(policy.adapter?{adapter:policy.adapter}:{})};
}

function bundleLauncher(files) {
  return files.find(file=>/^(?:iz2s\/modules\/[a-z0-9][a-z0-9-]{0,46}\/)?launcher\.(?:iza|izapp)$/.test(file.name));
}

async function prepareBundleAt(entries,candidate,extracted) {
  const image=bundleLauncher(entries);
  assert(image,'Include launcher.iza or launcher.izapp at the ZIP root or in iz2s/modules/<id>.');
  assert(image.bytes.length>=512&&image.bytes.length<=MAX_IMAGE,'Invalid SD launcher size');
  const prefix=image.name.slice(0,image.name.lastIndexOf('/')+1);
  assert(entries.every(file=>file.name.startsWith(prefix)),'The ZIP must contain a single SD application.');
  const members=prefix?entries.map(file=>({name:file.name.slice(prefix.length),bytes:file.bytes})):entries;
  const activeManifest='sd-bundle-'+archive.sha(image.bytes)+'.json';
  const files=members.filter(file=>!exportPreviews.has(file.name)&&
    (!qualifiedManifest.test(file.name)||file.name===activeManifest));
  const header=pkg.decode(image.bytes);
  const safeDefaults=new Map(defaults.files(header.id).map(file=>[file.name,file.bytes]));
  assert(files.length<=assets.MAX_ASSETS+3+safeDefaults.size&&files.every(file=>file.name===activeManifest||
    (safeDefaults.has(file.name)&&file.bytes.equals(safeDefaults.get(file.name)))||
    /^(?:launcher\.(?:iza|izapp)|sd-bundle\.json|asset-[a-f0-9]{64}\.izdata)$/.test(file.name)),
    'The SD bundle ZIP must contain only one application, its metadata, companion assets and standard export previews.');
  if(prefix)assert.equal(prefix,bundleRoot+'/'+header.id+'/','SD bundle folder and application ID differ.');
  const bundle=path.join(candidate,'.bundle');
  const input=path.join(bundle,'launcher.iza');
  try {
    await archive.writeFiles(bundle,files);
    const sources=await companionSources(input,image.bytes,header);
    assert.equal(files.filter(file=>file.name.startsWith('asset-')).length,sources.length,
      'The SD bundle ZIP contains undeclared companion assets.');
    for(const source of sources){
      const member=files.find(file=>file.name===source.path);
      assert(member&&member.bytes.length===source.bytes&&archive.sha(member.bytes)===source.sha256,
        header.delegated?'Managed SD source archive checksum differs.':'SD bundle companion checksum differs.');
    }
    return await prepareImage(input,candidate,extracted,image.bytes,header,sources);
  } finally {
    await fs.rm(bundle,{recursive:true,force:true});
  }
}

async function stage(out,prepareCandidate) {
  const output = path.join(out, 'packages');
  await fs.mkdir(output, { recursive: true });
  const candidate = await fs.mkdtemp(path.join(output, 'iz2s-core-sd-'));
  const extracted = path.join(candidate, '.source');
  try {
    return await prepareCandidate(candidate,extracted);
  } catch (error) {
    await fs.rm(candidate, { recursive: true, force: true });
    throw error;
  } finally {
    await fs.rm(extracted, { recursive: true, force: true });
  }
}

async function prepareBundle(files,out=runtime.output()) {
  return stage(out,(candidate,extracted)=>prepareBundleAt(files,candidate,extracted));
}

function prepareModule(module, out=runtime.output()) {
  return stage(out,(candidate,extracted)=>prepareArchive(module,candidate,extracted));
}

async function portableApp(prepared) {
  assert(prepared.format!=='.nro','Install this application from a mounted SD card.');
  const image = await fs.readFile(path.join(prepared.folder,'launcher.iza'));
  const executable=require('./installed-versions.cjs').identity(image);
  if (!prepared.assets) return {id:prepared.id,name:prepared.id+'.iza',bytes:image,packed:false,executable};
  const manifest = await fs.readFile(path.join(prepared.folder,'sd-bundle.json'));
  const companions = await Promise.all(JSON.parse(manifest).assets.map(async ({file:name})=>({
    name,bytes:await fs.readFile(path.join(prepared.folder,name)),
  })));
  const bytes = archive.encode([{name:'sd-bundle.json',bytes:manifest},{name:'launcher.iza',bytes:image},...companions,...defaults.files(prepared.id)]);
  assert(bytes.length<=archive.MAX_ARCHIVE,'Prepared SD bundle exceeds the archive transfer limit.');
  return {id:prepared.id,name:prepared.id+'.iza',bytes,packed:true,executable};
}

async function packageModule(module,out=runtime.output()) {
  const temporary = path.join(out,'tmp');
  await fs.mkdir(temporary,{recursive:true});
  const scratch = await fs.mkdtemp(path.join(temporary,'portable-app-'));
  try {
    const app=await portableApp(await prepareModule(module,scratch));
    const {descriptor}=module;
    return {...app,name:descriptor.switch?.downloadName??app.name,
      version:descriptor.version,sourceSha256:module.sha256};
  }
  finally { await fs.rm(scratch,{recursive:true,force:true}); }
}

async function prepare(input,out=runtime.output()) {
  return stage(out,async(candidate,extracted)=>{
    const file=await fs.open(input,'r');
    let bytes;
    try {
      const stat=await file.stat();
      assert(stat.isFile(),'Expected an app file');
      const magic=Buffer.alloc(8);
      await file.read(magic,0,magic.length,0);
      const executable=magic.toString('ascii')==='IZSDAPP1';
      assert(stat.size<=(executable?MAX_IMAGE:archive.MAX_ARCHIVE),'Application file is too large');
      bytes=await file.readFile();
    } finally {
      await file.close();
    }
    if(bytes.subarray(0,8).toString('ascii')==='IZSDAPP1'){
      assert(bytes.length>=512&&bytes.length<=MAX_IMAGE,'Invalid SD launcher size');
      return prepareImage(input,candidate,extracted,bytes);
    }
    assert(bytes.length<=archive.MAX_ARCHIVE,'Application bundle is too large.');
    const files=archive.decode(bytes,{allowDirectories:true});
    if(!files.some(file=>file.name==='module.json')&&
        (bundleLauncher(files)||files.some(file=>file.name==='sd-bundle.json')))
      return prepareBundleAt(files,candidate,extracted);
    const checked={...archive.inspectFiles(files),sha256:archive.sha(bytes),packageBytes:bytes.length};
    return prepareArchive(checked,candidate,extracted);
  });
}

if (require.main === module) {
  if (process.argv.length !== 3) {
    console.error('Usage: node engines/iz2s-core/tools/prepare-sd-app.cjs app.iza');
    process.exitCode = 2;
  } else prepare(process.argv[2]).then(result => console.log(JSON.stringify(result, null, 2)), error => {
    console.error(error.message);
    process.exitCode = 1;
  });
}
module.exports = { prepare, prepareBundle, bundleLauncher, packageModule, portableApp, MAX_IMAGE };
