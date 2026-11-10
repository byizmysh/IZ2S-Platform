// Explicit SD mutations. Uploads publish only after complete, flushed transfers.
const crypto = require('node:crypto'), path = require('node:path'), fs = require('node:fs/promises');
const transfer = require('./transfer.cjs');
const {virtualPath} = require('../../core/console/host/ftp-native.cjs');
function sdPath(value) {
  if (typeof value !== 'string' || !value.startsWith('/')) throw Error('Choose an absolute SD path');
  value = virtualPath('/', value);
  if (!value.startsWith('/sd/')) throw Error('Changes are supported inside /sd only');
  return value;
}
function create({getStorage, lease, busy = () => false, now = Date.now, checkpointFile}) {
  const uploads = new Map();
  let saving=Promise.resolve(),verification=null;
  function save(){const result=saving.catch(()=>{}).then(saveNow);saving=result;return result;}
  async function saveNow(){if(!checkpointFile)return;await fs.mkdir(path.dirname(checkpointFile),{recursive:true,mode:0o700});const rows=[...uploads].map(([token,r])=>({token,...r,file:null}));await fs.writeFile(checkpointFile+'.tmp',JSON.stringify(rows),{mode:0o600});await fs.rename(checkpointFile+'.tmp',checkpointFile);}
  const loaded=(async()=>{if(!checkpointFile)return;let rows;try{rows=JSON.parse(await fs.readFile(checkpointFile,'utf8'));}catch(e){if(e.code==='ENOENT')return;throw e;}if(!Array.isArray(rows))throw Error('Invalid upload checkpoints');for(const r of rows){if(!/^[a-f0-9]{48}$/.test(r.token)||!Number.isSafeInteger(r.size)||r.size<0||!Number.isSafeInteger(r.offset)||r.offset<0||r.offset>r.size||!Array.isArray(r.chunks)||r.temporary!==path.posix.dirname(sdPath(r.path))+'/.ns2-upload-'+r.token+'.part')throw Error('Invalid upload checkpoint');let at=0;for(const c of r.chunks){if(c.offset!==at||!Number.isInteger(c.bytes)||c.bytes<1||c.bytes>transfer.CHUNK_BYTES||!/^[a-f0-9]{64}$/.test(c.sha256))throw Error('Invalid upload checksum');at+=c.bytes;}if(at!==r.offset)throw Error('Invalid upload offset');uploads.set(r.token,{...r,file:null,paused:true});}})();
  const describe=(token,r)=>({token,path:r.path,bytes:r.offset,size:r.size,chunkBytes:r.chunkBytes,fingerprint:r.fingerprint||null,paused:!r.file,chunks:r.chunks});
  async function pause(token){const r=uploads.get(token);if(!r)return {ok:true};if(r.file&&r.lease===lease()&&getStorage()){await getStorage().flush(r.file);await getStorage().close(r.file);}r.file=null;r.paused=true;await save();return {ok:true,...describe(token,r)};}
  async function resume(token,value){const r=uploads.get(token),storage=getStorage();if(!r)throw Error('Upload checkpoint is unavailable');if(value.lease!==lease()||value.path!==r.path||value.size!==r.size||value.fingerprint!==r.fingerprint)throw Error('Choose the same file to resume this upload');if(r.file&&r.lease===lease()){r.deadline=now()+120000;return describe(token,r);}
    const names=await storage.list(path.posix.dirname(r.path));if(names.some(e=>e.name===path.posix.basename(r.path)))throw Error('The destination already exists. Refresh the folder');
    verification={token,phase:'verifying',bytes:0,total:r.offset};
    let file;try{file=await storage.openRead(r.temporary);
      if(file.size!==r.size)throw Error('The partial SD file changed');
      for(const c of r.chunks){
        let digest;
        if(typeof storage.hash==='function')digest=await storage.hash(file,c.offset,c.bytes);
        else {const parts=[];let read=0;while(read<c.bytes){const b=await storage.read(file,c.offset+read,c.bytes-read);if(!b.length)throw Error('The partial SD file is incomplete');parts.push(b);read+=b.length;}digest=crypto.createHash('sha256').update(Buffer.concat(parts)).digest('hex');}
        if(digest!==c.sha256)throw Error('The partial SD file failed verification. Cancel this transfer and upload it again');
        verification.bytes=c.offset+c.bytes;
      }
    }finally{try{if(file)await storage.close(file);}finally{verification=null;}}

    r.file=await storage.resumeWrite(r.temporary,r.size);r.lease=lease();r.paused=false;r.deadline=now()+120000;r.chunkBytes=transfer.chunkBytes(storage);await save();return describe(token,r);
  }
  async function cancel(token) {
    const record = uploads.get(token); if (!record) return {ok: true};
    if (record.file&&record.lease===lease()) {await getStorage().close(record.file); record.file = null;}
    await getStorage().remove(record.temporary, false); uploads.delete(token); await save();return {ok: true};
  }
  async function handle(method, value) {
    await loaded;
    if(method==='sd-upload-list')return {uploads:[...uploads].map(([t,r])=>describe(t,r))};
    const storage = getStorage(); if (!storage) throw Error('Start SD & console storage first');
    for (const [token, record] of uploads) if (record.file&&record.deadline < now()) await pause(token);
    if(method==='sd-upload-resume')return resume(value.token,value);
    if(method==='sd-upload-pause')return pause(value.token);
    if(method==='sd-upload-status'){const r=uploads.get(value.token);if(!r)throw Error('Upload checkpoint is unavailable');return describe(value.token,r);}
    if (method === 'sd-expire') return {ok: true};
    if (method === 'sd-upload-cancel') return cancel(value.token);
    if (method === 'sd-upload-chunk' || method === 'sd-upload-finish') {
      const record = uploads.get(value.token); if (!record || !record.file || record.lease !== lease()) throw Error('Upload session expired');
      record.deadline = now() + 120000;
      if (method === 'sd-upload-chunk') {
        if (value.offset !== record.offset || typeof value.data !== 'string' || value.data.length > Math.ceil(record.chunkBytes / 3) * 4 || !/^[A-Za-z0-9+/]*={0,2}$/.test(value.data)) throw Error('Invalid upload chunk');
        const bytes = Buffer.from(value.data, 'base64');
        if (!bytes.length || bytes.length > record.chunkBytes || record.offset + bytes.length > record.size) throw Error('Invalid upload length');
        await storage.write(record.file, record.offset, bytes);await storage.flush(record.file);record.chunks.push({offset:record.offset,bytes:bytes.length,sha256:crypto.createHash('sha256').update(bytes).digest('hex')}); record.offset += bytes.length;await save();
        return {bytes: record.offset};
      }
      if (record.offset !== record.size) throw Error('Upload is incomplete');
      await storage.flush(record.file); await storage.close(record.file); record.file = null;
      await storage.rename(record.temporary, record.path); uploads.delete(value.token);await save();
      return {ok: true, path: record.path, bytes: record.size};
    }
    if (busy() || uploads.size) throw Error('Wait for active transfers or cancel them before changing files');
    if (value.lease !== lease()) throw Error('The export changed. Refresh the folder before changing files');
    const target = sdPath(value.path);
    if (method === 'sd-upload-begin') {
      if (value.lease !== lease()) throw Error('The export changed. Start the upload again');
      if (!Number.isSafeInteger(value.size) || value.size < 0) throw Error('Invalid upload size');
      const names = await storage.list(path.posix.dirname(target));
      if (names.some(entry => entry.name === path.posix.basename(target))) throw Error('A file or folder with that name already exists');
      const token = crypto.randomBytes(24).toString('hex'), temporary = path.posix.dirname(target) + '/.ns2-upload-' + token + '.part';
      const file = await storage.openWrite(temporary, value.size);
      const chunkBytes = transfer.chunkBytes(storage);
      uploads.set(token, {path: target, temporary, file, size: value.size, offset: 0, chunkBytes, lease: lease(), chunks:[], fingerprint:typeof value.fingerprint==='string'?value.fingerprint:null, deadline: now() + 120000});
      await save();return {token, bytes: 0, chunkBytes};
    }
    if (method === 'sd-mkdir') {
      const parts = target.slice(4).split('/'); let parent = '/sd';
      for (let i = 0; i < parts.length; i++) {
        const names = await storage.list(parent), existing = names.find(entry => entry.name === parts[i]);
        parent += '/' + parts[i];
        if (existing) {if (existing.type !== 'dir' || (!value.parents && i === parts.length - 1)) throw Error('A file or folder with that name already exists');}
        else {if (!value.parents && i !== parts.length - 1) throw Error('Parent folder does not exist'); await storage.mkdir(parent);}
      }
      return {ok: true};
    }
    if (method === 'sd-rename') {
      const to = sdPath(value.to);
      if (target === to) return {ok: true};
      if (to.startsWith(target + '/')) throw Error('A folder cannot be moved inside itself');
      const names = await storage.list(path.posix.dirname(to));
      if (names.some(entry => entry.name === path.posix.basename(to))) throw Error('A file or folder with that name already exists');
      await storage.rename(target, to); return {ok: true};
    }
    if (method === 'sd-delete') {
      if (value.confirmed !== true) throw Error('Confirm permanent deletion first');
      const stat = await storage.stat(target); await storage.remove(target, stat.type === 'dir', true); return {ok: true};
    }
    throw Error('Unknown SD action');
  }
  return {handle, ready:loaded, progress:()=>verification&&({...verification}), detach:async()=>{await loaded;for(const r of uploads.values()){r.file=null;r.paused=true;}await save();}, active:()=>[...uploads.values()].filter(r=>!!r.file).length, count: () => uploads.size, close: async () => {await loaded;for (const token of [...uploads.keys()]) await pause(token);}};
}
module.exports = {create, sdPath};
