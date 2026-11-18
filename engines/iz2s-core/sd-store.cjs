'use strict';
const fs = require('node:fs/promises');
const { constants: flags } = require('node:fs');
const path = require('node:path');
const assert = require('node:assert/strict');
const { randomUUID } = require('node:crypto');
const { mountInfo } = require('../../core/platform/cardputer-media.cjs');
const sd = require('../../core/console/sd-launcher/install.cjs');
const packages = require('../../core/console/sd-launcher/package.cjs');
const archive = require('../../core/archive.cjs');
const assets = require('../../core/console/sd-launcher/assets.cjs');
const { prepare, MAX_IMAGE } = require('./tools/prepare-sd-app.cjs');
const { remove } = require('../../core/console/sd-launcher/remove.cjs');
const runtime = require('./runtime.cjs');
const nro = require('./tools/nro-package.cjs');
const versions = require('./tools/installed-versions.cjs');
const json = require('../../core/packages/files.cjs');
const { eligibility, buildApp, directImage } = require('./sd-compat.cjs');
const idPattern = /^[a-z0-9][a-z0-9-]{0,46}$/;
const base = '/sd' + require('../../core/module-storage.cjs').BASE;

async function discover() {
  if (process.platform !== 'linux') return [];
  const entries=(await fs.readFile('/proc/self/mountinfo','utf8')).split('\n');
  const volumes=entries.flatMap(line=>{
    const [mountId,,,,,options]=line.split(' ');
    return options?.split(',').includes('rw')?mountInfo(line).map(v=>({...v,mountId})):[];
  }).filter(
    v => v.source.startsWith('/dev/') && ['vfat','exfat'].includes(v.fstype) &&
      /^\/(?:run\/media|media)\//.test(v.target));
  const cards = await Promise.all(volumes.map(async v => {
    try {
      const root = await fs.lstat(v.target);
      const folder = await fs.lstat(path.join(v.target, 'iz2s'));
      const menu = await fs.lstat(path.join(v.target, 'iz2s', 'izlauncher.iza'));
      if (root.isDirectory() && folder.isDirectory() && menu.isFile() &&
          root.dev === folder.dev && root.dev === menu.dev)
        return { target:v.target,source:v.source,device:root.dev,inode:root.ino,mountId:v.mountId };
    } catch (e) { if (!['ENOENT','EACCES','ENODEV','EIO'].includes(e.code)) throw e; }
    return null;
  }));
  return cards.filter(Boolean);
}

// The existing publisher owns menu package filenames, readback and rollback.
// A held directory FD anchors all operations to the selected Linux mount.
async function storage(card, action) {
  const root = await fs.open(card.target, flags.O_RDONLY | flags.O_DIRECTORY | flags.O_NOFOLLOW);
  const dir = '/proc/self/fd/' + root.fd;
  const held = new Set(), dirty = new Set();
  const same = stat => stat.isDirectory() && stat.dev === card.device && stat.ino === card.inode;
  async function checked() {
    assert(same(await root.stat()) && same(await fs.lstat(card.target)),
      'The selected IZ2S SD card changed. Reconnect it and select it again.');
  }
  function scoped(value) {
    const parent = ['/sd','/sd/iz2s',base].includes(value);
    const [id, name, ...extra] = value.slice(base.length + 1).split('/');
    assert(parent || (value.startsWith(base + '/') && !extra.length && idPattern.test(id) &&
      (name === undefined ||
        /^(?:launcher\.(?:iza|nro)|asset-[a-f0-9]{64}\.izdata|\.(?:launcher|launcher-nro|asset)-[a-f0-9]{16}\.(?:tmp|bak)|\.launcher-update\.bak|\.removed-[a-f0-9]{16}\.iza)$/.test(name))),
      'SD path is outside the IZ2S application store');
    return value.split('/').slice(2);
  }
  async function file(value) {
    await checked();
    const parts = scoped(value);
    let full = dir;
    for (const name of parts.slice(0,-1)) {
      full = path.join(full,name);
      const item = await fs.lstat(full);
      assert(item.isDirectory() && item.dev === card.device, 'SD directory escapes this card');
    }
    return parts.length ? path.join(full,parts.at(-1)) : full;
  }
  async function entry(value, mode) {
    const full = await file(value);
    const info = value === '/sd' ? await root.stat() : await fs.lstat(full);
    assert(info.dev === card.device && (mode === 'dir' ? info.isDirectory() : info.isFile()),
      'Unexpected or unsafe SD entry');
    return full;
  }
  const known = fn => async (...args) => {
    try { return await fn(...args); }
    catch (e) {
      if (e.code === 'ENOENT') e.nativeResult = 0x202;
      else if (['EEXIST','EACCES','EPERM','ENOSPC','EROFS','ENOTEMPTY'].includes(e.code)) e.nativeResult = e.code;
      throw e;
    }
  };
  async function open(value,size) {
    const full = size === undefined ? await entry(value,'file') : await file(value);
    const fd = await fs.open(full, flags.O_NOFOLLOW | (size === undefined ? flags.O_RDONLY :
      flags.O_RDWR | flags.O_CREAT | flags.O_EXCL), 0o600);
    held.add(fd);
    const info = await fd.stat();
    assert(info.isFile() && info.dev === card.device, 'Unexpected SD file');
    if(size !== undefined) dirty.add(path.dirname(full));
    return {fd,size:size ?? info.size};
  }
  const io = {
    writableMounts:['sd'], uncertain:false,
    list:known(async value => (await fs.readdir(await entry(value,'dir'),{withFileTypes:true})).map(
      item => ({name:item.name,type:item.isDirectory()?'dir':item.isFile()?'file':'other'}))),
    mkdir:known(async value => {
      const full = await file(value);
      await fs.mkdir(full); dirty.add(path.dirname(full));
    }),
    openRead:known(value=>open(value)), openWrite:known(open),
    async read({fd,size},at,n) {
      assert(at>=0 && n>0 && at+n<=size);
      const b=Buffer.alloc(n);
      for(let i=0;i<n;){const r=await fd.read(b,i,n-i,at+i);assert(r.bytesRead,'Short SD read');i+=r.bytesRead;}
      return b;
    },
    async write({fd,size},at,b) {
      assert(at>=0 && b.length>0 && at+b.length<=size);
      for(let i=0;i<b.length;){const r=await fd.write(b,i,b.length-i,at+i);assert(r.bytesWritten,'Short SD write');i+=r.bytesWritten;}
    },
    flush:({fd})=>fd.sync(), async close({fd}) {await fd.close();held.delete(fd);},
    rename:known(async (from,to)=>{
      const src=await entry(from,'file'), dest=await file(to);
      assert(path.dirname(src)===path.dirname(dest),'SD rename left the module folder');
      const existing=await fs.lstat(dest).catch(e=>{if(e.code!=='ENOENT')throw e;return null;});
      if(existing)throw Object.assign(Error('The SD destination already exists'),{code:'EEXIST'});
      await fs.rename(src,dest); dirty.add(path.dirname(src));
    }),
    remove:known(async value=>{const full=await entry(value,'file');await fs.unlink(full);dirty.add(path.dirname(full));}),
    removeModuleDirectory: known(async value => {
      assert(value.startsWith(base + '/') && idPattern.test(value.slice(base.length + 1)),
        'Select an installed module folder.');
      const full = await entry(value, 'dir');
      async function checkTree(folder) {
        for (const name of await fs.readdir(folder)) {
          const child = path.join(folder, name);
          const info = await fs.lstat(child);
          assert(info.dev === card.device && (info.isFile() || info.isDirectory()),
            'The module folder contains an unsafe entry.');
          if (info.isDirectory()) await checkTree(child);
        }
      }
      await checkTree(full);
      await checked();
      await fs.rm(full, {recursive: true});
      dirty.add(path.dirname(full));
    }),
    async commit() {
      await checked();
      for(const folder of dirty) {
        const fd=await fs.open(folder,flags.O_RDONLY|flags.O_DIRECTORY);
        try { await fd.sync(); } finally { await fd.close(); }
      }
      dirty.clear();
    },
  };
  try { await checked(); return await action(io); }
  finally {
    try { await Promise.allSettled([...held].map(fd=>fd.close())); }
    finally { await root.close(); }
  }
}

function create({root,out,preview=false,find=discover,readPackage=require('../../core/modules.cjs').read,
  build=packages.build}={}) {
  let selected=null, selection=null, apps=[], queue=Promise.resolve(), needsRefresh=false;
  const versionFile=path.join(root||path.join(__dirname,'../..'),'data/state/core-sd-versions.json');
  async function remember(result,source){
    if(!result.readbackVerified||!source.version)return;
    const prior=await json.read(versionFile,[]);
    const records=Array.isArray(prior)?prior:[];
    const record={id:result.id,bytes:result.bytes,sha256:result.sha256,version:source.version,sourceSha256:source.sourceSha256};
    await json.write(versionFile,[record,...records.filter(row=>row.id!==record.id||row.sha256!==record.sha256)].slice(0,128));
  }
  function serial(fn){const work=queue.then(fn);queue=work.catch(()=>{});return work;}
  const same=(a,b)=>a && b && ['target','source','device','inode','mountId'].every(k=>a[k]===b[k]);
  const guard=()=>assert(!preview,'Preview cannot access the IZ2S SD store');
  async function current(requested) {
    guard();
    assert(requested&&requested===selection,'The selected IZ2S SD card changed. Refresh the panel and retry.');
    assert(selected&&(await find()).some(v=>same(v,selected)),
      'Connect the IZ2S SD card to this PC and select it in App library.');
    return selected;
  }
  async function refresh(card){
    const saved=await json.read(versionFile,[]);
    const records=Array.isArray(saved)?saved:[];
    const identifyDigest=(id,bytes)=>[...records,...versions.official].some(row=>row.id===id&&row.bytes===bytes);
    apps=await storage(card,async io=>[
      ...(await sd.inventory(io,{identities:true,identifyDigest})).map(app=>versions.label({...app,format:'.iza'},app.sha256,records)),
      ...await nro.inventory(io),
    ]);
    needsRefresh=false;
  }
  async function status() {
    if(preview||!selected)return {selected:false,ready:false,apps:[]};
    const ready=(await find().catch(()=>[])).some(v=>same(v,selected));
    return {selected:true,selection,ready:ready&&!needsRefresh,target:selected.target,apps:ready&&!needsRefresh?apps:[]};
  }
  function ids(value){
    assert(Array.isArray(value)&&value.length>0&&value.length<=15&&value.every(id=>typeof id==='string'&&idPattern.test(id)), 'Choose up to 15 applications');
    return [...new Set(value)];
  }
  async function mutation(card,fn){
    needsRefresh=true;
    const result=await storage(card,fn);
    await refresh(card);
    return result;
  }
  return {status,
    volumes:async()=>{guard();return (await find()).map(({target})=>({target}));},
    select:target=>serial(async()=>{
      guard();
      if(target===null){selected=null;selection=null;apps=[];needsRefresh=false;return status();}
      const card=(await find()).find(v=>v.target===target);
      assert(card,'Select a mounted Switch SD card from the list');
      await refresh(card);selected=card;selection=randomUUID();return status();
    }),
    refresh:key=>serial(async()=>{await refresh(await current(key));return status();}),
    load:(value,key)=>serial(async()=>{
      await current(key);const requests=ids(value);
      assert(!needsRefresh,'Refresh the IZ2S SD inventory before changing apps.');
      const output=path.join(runtime.output(out),'tmp');await fs.mkdir(output,{recursive:true});
      const scratch=await fs.mkdtemp(path.join(output,'core-store-'));
      try {
        const rows=[];
        for(const id of requests){
          const m=await readPackage(root,id,{pinned:false});
          const app=archive.switchApp(m);
          const {entry, manifest, files}=app;
          assert.equal(manifest.id,id,'Switch application identity differs');
          const direct = directImage(m.descriptor, m.files);
          if (direct) {
            rows.push({bytes:direct.bytes, assets:[], version:m.descriptor.version, sourceSha256:m.sha256});
            continue;
          }
          const program=await nro.moduleProgram(m,app);
          if(program){rows.push({id,program});continue;}
          const policy=eligibility(manifest,m.descriptor,files,{sha256:m.sha256,archiveFiles:m.files});
          assert(policy.compatible,policy.reason);
          const source=path.join(scratch,id,'switch');
          await archive.writeFiles(source, files);
          const prepared=await buildApp(source,path.join(scratch,id,'output'),policy,{build,entry:path.posix.basename(entry)});
          rows.push({...prepared,version:m.descriptor.version,sourceSha256:m.sha256});
        }
        return {installed:await mutation(await current(key),async io=>{
          const rowsInstalled=[];
          for(const row of rows){
            if(row.program){rowsInstalled.push(await nro.install(io,row.id+'.nro',row.program));continue;}
            const result=await sd.installBytes(io,row.bytes,{assets:row.assets});
            await remember(result,row);
            rowsInstalled.push(result);
          }
          return rowsInstalled;
        })};
      } finally { await fs.rm(scratch,{force:true,recursive:true}); }
    }),
    importApp:(stream,format,key,fileName)=>serial(async()=>{
      await current(key);
      assert(!needsRefresh,'Refresh the IZ2S SD inventory before changing apps.');
      assert(['iza','nro'].includes(format),'Select an .iza file.');
      if(format==='nro')nro.idFor(fileName);
      const max=format==='nro'?nro.MAX_NRO:MAX_IMAGE;
      const output=path.join(runtime.output(out),'tmp');await fs.mkdir(output,{recursive:true});
      const scratch=await fs.mkdtemp(path.join(output,'core-import-'));
      try {
        const input=path.join(scratch,'upload.'+format);
        const file=await fs.open(input,'wx',0o600);
        let bytes=0;
        try {
          for await(const chunk of stream){
            assert(bytes+chunk.length<=max,'Application file is too large.');
            await file.writeFile(chunk);
            bytes+=chunk.length;
          }
          assert(bytes>0,'The application file is empty.');
        } finally { await file.close(); }
        if(format==='nro'){
          const image=await fs.readFile(input);
          await nro.metadata(image.length,async(at,length)=>image.subarray(at,at+length));
          return await mutation(await current(key),io=>nro.install(io,fileName,image));
        }
        const result=await prepare(input,scratch);
        if(result.format==='.nro'){
          const program=await fs.readFile(path.join(result.folder,'launcher.nro'));
          return await mutation(await current(key),io=>nro.install(io,result.id+'.nro',program));
        }
        const image=path.join(result.folder,'launcher.iza');
        const contents=await fs.readFile(image);
        const companions=await assets.loadBundle(image,contents);
        return await mutation(await current(key),async io=>{
          const installed=await sd.installBytes(io,contents,{assets:companions});
          await remember(installed,result);
          return installed;
        });
      } finally { await fs.rm(scratch,{recursive:true,force:true}); }
    }),
    unload:(value,key)=>serial(async()=>{
      const card=await current(key), requests=ids(value);
      assert(!needsRefresh,'Refresh the IZ2S SD inventory before changing apps.');
      return {removed:await mutation(card,async io=>{
        const installed = new Set((await sd.inventory(io)).map(row => row.id));
        const nroApps = requests.some(id => !installed.has(id))
          ? new Map((await nro.inventory(io)).map(app => [app.id, app])) : new Map();
        assert(requests.every(id => installed.has(id) || nroApps.has(id)), 'An application is no longer installed');
        const rows = [];
        for (const id of requests) {
          rows.push(installed.has(id)
            ? await remove(io, id, {lease: {active: true}})
            : await nro.remove(io, nroApps.get(id)));
        }
        return rows;
      })};
    }),
  };
}
module.exports = { create, discover, storage };
