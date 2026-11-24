'use strict';

const assert = require('node:assert/strict');
const {randomUUID} = require('node:crypto');
const path = require('node:path');
const link = require('../engines/iz2s-core/tools/pc-link-service.cjs');
const {pcmBytes,blitbunProfile} = require('../engines/iz2s-core/tools/pc-stream.cjs');

const origin = 'http://127.0.0.1:39525';
const stream = origin + '/stream/blitbun';

function create({root,out,ensure=link.ensure,fetch=globalThis.fetch,idleMs=8000}) {
  let capture, closing, nativeBusy=false;
  function touch(session) {
    clearTimeout(session.timer);
    session.timer=setTimeout(()=>{ void stop(session.id).catch(()=>{}); },idleMs);
    session.timer.unref();
  }
  async function profile(width) {
    const set=width!==undefined;
    if(set)blitbunProfile(width);
    const response=await fetch(stream+'/profile'+(set?'?width='+width:''),{
      method:set?'POST':'GET',headers:{origin},signal:AbortSignal.timeout(3000),
    });
    const value=await response.json();
    if(!response.ok)throw Error(value.error||'Could not read the BlitBun resolution.');
    return value;
  }
  async function begin() {
    assert(!capture&&!closing&&!nativeBusy,'Stop the current BlitBun source first.');
    const session={id:randomUUID(),pending:new Map(),controller:new AbortController()};
    capture=session;
    try {
      const status=await ensure(out);
      assert(capture===session,'Browser capture ended. Choose a browser tab again.');
      assert(status?.service?.root===path.resolve(root),'Start PC Link from this 0.3.2 installation.');
      assert(!['starting','running','retrying'].includes(status.media?.blitbun?.phase),'Stop the current BlitBun source first.');
      const selected=status.blitbunProfile||blitbunProfile();
      session.initialWidth=selected.width;
      touch(session);
      return {id:session.id,...selected};
    } catch(error) {
      if(capture===session) capture=null;
      throw error;
    }
  }
  async function relay(req, kind) {
    const session=capture;
    assert(session&&req.headers['x-blitbun-capture']===session.id,'Browser capture ended. Choose a browser tab again.');
    assert(req.headers['content-type']==='application/octet-stream','Expected encoded media data.');
    assert(!session.pending.has(kind),'A browser media packet is already in progress.');
    const requested=req.headers['x-blitbun-width'];
    const {width,height}=kind==='audio'?{}:blitbunProfile(requested===undefined?session.initialWidth:Number(requested));
    const bytes=kind==='audio'?pcmBytes:width*height/2;
    assert.equal(Number(req.headers['content-length']),bytes,'Invalid browser media packet size.');
    const destroy=()=>{if(!req.complete)req.destroy();};
    req.setTimeout(4000,destroy);
    session.controller.signal.addEventListener('abort',destroy,{once:true});
    const reading=(async()=>{
      const packet=Buffer.allocUnsafe(bytes);
      let length=0;
      for await(const chunk of req) {
        session.controller.signal.throwIfAborted();
        assert(chunk.length<=bytes-length,'Invalid browser media packet size.');
        chunk.copy(packet,length);
        length+=chunk.length;
      }
      assert.equal(length,bytes,'Incomplete browser media packet.');
      session.controller.signal.throwIfAborted();
      const target=kind==='audio'?stream+'/pcm?browser=1':stream+'?width='+width+'&height='+height+'&format=1&browser=1';
      const response=await fetch(target,{method:'POST',headers:{origin,'content-type':'application/octet-stream'},
        body:packet,signal:AbortSignal.timeout(4000)});
      const result=await response.json();
      if(!response.ok)throw Error('PC Link rejected the browser media.');
      if(kind==='frame'&&capture===session) touch(session);
      return kind==='frame'?{ok:true,profile:result.profile}:{ok:true};
    })();
    session.pending.set(kind,reading);
    try {return await reading;} finally {
      req.setTimeout(0);
      session.controller.signal.removeEventListener('abort',destroy);
      session.pending.delete(kind);
    }
  }
  async function stop(id) {
    const session=capture;
    if(!session||session.id!==id)return {ok:true};
    capture=null;
    clearTimeout(session.timer);
    session.controller.abort();
    closing=(async()=>{
      await Promise.allSettled(session.pending.values());
      const response=await fetch(stream,{method:'DELETE',headers:{origin},signal:AbortSignal.timeout(4000)});
      await response.arrayBuffer();
      if(!response.ok)throw Error('Could not clear the browser media from PC Link.');
    })();
    try {await closing;} finally {closing=null;}
    return {ok:true};
  }
  async function native(action) {
    assert(!capture&&!closing&&!nativeBusy,'Stop the current BlitBun source first.');
    nativeBusy=true;
    try {return await action();} finally {nativeBusy=false;}
  }
  return {begin,relay,stop,native,profile,close:()=>capture?stop(capture.id):closing};
}

module.exports={create};
