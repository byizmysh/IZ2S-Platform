'use strict';
const assert = require('node:assert/strict');
const {randomUUID,createHash} = require('node:crypto');
const path = require('node:path');
const {virtualPath} = require('../../../core/console/host/ftp-native.cjs');
const {MAX_IMAGE,MAX_PACKAGE} = require('./pc-link.cjs');

const ID = 'iz2s-pc-link';
const CHUNK = 256 * 1024;
const directory = {type:'dir',size:0};

function sizeAt(buffer, offset){
  const size=Number(buffer.readBigUInt64BE(offset));
  assert(Number.isSafeInteger(size),'Unsupported SD file size.');
  return size;
}

function rows(buffer){
  const entries=[];
  const decoder=new TextDecoder('utf-8',{fatal:true});
  for(let at=0;at<buffer.length;){
    assert(at+10<=buffer.length,'Truncated SD directory.');
    const end=at+10+buffer[at+1];
    assert(buffer[at]<=1&&end>at+10&&end<=buffer.length,'Invalid SD directory entry.');
    let name;
    try {name=decoder.decode(buffer.subarray(at+10,end));}
    catch {at=end;continue;}
    assert(!/[\x00-\x1f\x7f/\\]/.test(name)&&!['.','..'].includes(name),'Invalid SD entry name.');
    entries.push({name,type:buffer[at]?'dir':'file',size:sizeAt(buffer,at+2)});
    at=end;
  }
  return entries;
}

function queueName(target){
  assert(/^\/apps\/[^/]+\.iza$/i.test(target),'Upload an .iza file in /apps.');
  return target.slice(6);
}

function create(bridge,ftp,{games=require('./pc-link-games.cjs').create(bridge),directFtp=false}={}){
  const session=()=>({id:ID,host:ftp?.address?.()??bridge.data().consoleAddress});
  let lease=randomUUID(), upload=null, stopped=false;
  const downloads=new Map();
  const writes=new Set();
  let resetting=null, cleaning=false;
  const pendingCleanup=new Set();
  let resetEpoch=bridge.data().resetEpoch;
  let gameState=null,gameStarting=false;
  const state=(ftpReady=false)=>({status:stopped?'idle':directFtp&&!ftpReady?'unavailable':'ready',mode:'storage',
    name:directFtp?'Switch 2 SD card':'IZ2S SD and Add App queue',
    lease,chunkBytes:CHUNK,sdFtp:ftpReady,ftpHost:ftpReady?ftp?.address?.()??null:null,
    directFtp,ftpError:ftpReady?'':ftp?.problem?.()||'',
    mounts:[...(directFtp?[]:['apps']),...(ftpReady||bridge.data().files?['sd']:[])]});
  const onSd=target=>target==='/sd'||target?.startsWith('/sd/');
  function sdPath(target){
    assert(onSd(target),'Choose a path on /sd.');
    return target.slice(3)||'/';
  }
  async function available(){
    const ready=!!(await ftp?.online());
    if(ready&&pendingCleanup.size&&!cleaning){
      cleaning=true;
      void (async()=>{
        for(const stage of pendingCleanup){
          try{
            await ftp.call('discard-stage',stage);
            pendingCleanup.delete(stage);
          }catch{break;}
        }
      })().finally(()=>{cleaning=false;});
    }
    return ready;
  }
  async function discard(item){
    if(!item?.stage)return;
    if(!item.cleanup)item.cleanup=(async()=>{
      await item.pipe?.closed.catch(()=>{});
      try{await ftp.call('discard-stage',item.stage);}
      catch(error){pendingCleanup.add(item.stage);throw error;}
    })();
    await item.cleanup;
  }
  function closeDownload(item){
    item?.controller?.abort(Error('FTP download cancelled.'));
    if(item){item.pipe=null;item.file=null;}
  }
  function reset(){
    if(resetting)return resetting;
    resetEpoch=bridge.data().resetEpoch;
    games.reset();
    const gameStop=gameState?games.rpc.request('stop',{lease:gameState.lease,cancelDownloads:true}):Promise.resolve();
    gameState=null;
    lease=randomUUID();
    const previous=upload;
    upload=null;
    if(previous){
      clearTimeout(previous.timer);
      previous.controller.abort(Error('The storage session was reset.'));
    }
    for(const controller of writes)controller.abort(Error('The storage session was reset.'));
    for(const item of downloads.values())closeDownload(item);
    downloads.clear();
    resetting=Promise.all([gameStop,(previous?.finishing??discard(previous)).catch(error=>{
      if(!previous?.finishing)throw error;
    })]).finally(()=>{resetting=null;});
    return resetting;
  }
  bridge.onReset?.(reset);
  function ensureLease(token){
    assert(!gameState&&!gameStarting&&!stopped&&!resetting&&token===lease&&bridge.data().resetEpoch===resetEpoch,
      'Reconnect to Super File Bros. storage.');
  }
  function ensureCurrent(item){
    ensureLease(item.lease);
    assert(upload===item,'The upload has expired.');
    item.controller.signal.throwIfAborted();
  }
  async function write(args,operation,...params){
    ensureLease(args.lease);
    assert(ftp&&await available(),ftp?.problem?.()||'Enable IZ2S FTP to write on the SD card.');
    ensureLease(args.lease);
    const controller=new AbortController();
    writes.add(controller);
    try{return await ftp.callWithSignal(controller.signal,operation,...params);}
    finally{writes.delete(controller);}
  }
  function cancel(item,reason){
    clearTimeout(item.timer);
    item.controller.abort(Error(reason));
    return item.finishing?.catch(()=>{})??discard(item);
  }
  async function publish(item){
    const {name,stage,size}=item;
    const expected=item.hash.digest('hex');
    const signal=item.controller.signal;
    await item.pipe.finish();
    ensureCurrent(item);
    await ftp.callWithSignal(signal,'verify-stage',stage,String(size),...(item.verify?[expected]:[]));
    ensureCurrent(item);
    try{
      await ftp.callWithSignal(signal,'publish',name,stage);
      return {ok:true,verified:item.verify?'sha256':'size'};
    }catch(error){
      if(/destination already exists/i.test(error.message))throw error;
      let actual;
      try{
        const info=await ftp.call('stat',name);
        if(info?.type==='file'&&info.size===size)actual=await ftp.call('digest',name);
      }catch(cause){
        throw Error('Could not confirm whether the SD file was published. '+cause.message,{cause:error});
      }
      if(actual===expected)return {ok:true,verified:'sha256',recovered:true};
      throw Error('The upload was not published. '+error.message,{cause:error});
    }
  }
  function transfer(operation,target,offset=0,length=0){
    const name=Buffer.from(target.slice(3)||'/');
    assert((target==='/sd'||target.startsWith('/sd/'))&&name.length<=768,'Choose a path on /sd.');
    const request=Buffer.alloc(13+name.length);
    request.write(operation);
    request.writeBigUInt64BE(BigInt(offset),1);
    request.writeUInt32BE(length,9);
    name.copy(request,13);
    return bridge.files(request);
  }
  async function stat(target){
    if(target==='/'||target==='/apps')return directory;
    if(target.startsWith('/apps/')){
      const item=bridge.queuedPackage();
      assert(item&&item.name===queueName(target),'That package is no longer queued.');
      return {type:'file',size:item.bytes.length};
    }
    if(await available()){
      const info=await ftp.call('stat',sdPath(target));
      assert(info,'The selected entry no longer exists.');
      return info;
    }
    const data=await transfer('T',target);
    assert(data.length===9&&data[0]<=1,'Invalid SD stat response.');
    return {type:data[0]?'dir':'file',size:sizeAt(data,1)};
  }
  async function browse(target){
    if(target==='/')return {cwd:target,writable:false,entries:state(await available()).mounts.map(name=>({name,...directory}))};
    if(target==='/apps'){
      const item=bridge.queuedPackage();
      return {cwd:target,writable:true,entries:item?[{name:item.name,type:'file',size:item.bytes.length}]:[]};
    }
    if(await available())return {cwd:target,writable:true,entries:await ftp.call('list',sdPath(target))};
    return {cwd:target,writable:false,entries:rows(await transfer('B',target))};
  }
  function draft(token){assert(upload&&upload.token===token,'The upload has expired.');return upload;}
  function download(token){const item=downloads.get(token);assert(item,'The download has expired.');return item;}
  function inside(root, target){return target===root||target.startsWith((root==='/'?'':root)+'/');}
  return {
    busy:()=>!!(upload||downloads.size||writes.size||resetting||gameStarting),
    discover:async()=>{
      if(directFtp&&!await available())throw Error(ftp.problem()||'The FTP session has ended. Enable it again on Switch 2.');
      return {...session(),run:ID};
    },
    async request(op,args={}){
      const epoch=bridge.data().resetEpoch;
      if(epoch!==resetEpoch){
        await reset();
      }
      const target=args.path===undefined?null:virtualPath('/',args.path);
      if(directFtp&&target&&target!=='/'&&!onSd(target))throw Error('Choose a path on /sd.');
      switch(op){
        case 'status': return gameState?await games.rpc.request('status'):state(await available());
        case 'list': return {...await games.list(args),session:session()};
        case 'start': {
          assert(!directFtp,'An open FTP channel grants access to the SD card only.');
          assert(!gameStarting&&!upload&&!downloads.size&&!writes.size&&!resetting,'Wait for file transfers to finish before opening a game.');
          gameStarting=true;stopped=false;const startEpoch=resetEpoch;
          try{
            const result=await games.rpc.request('start',args);
            if(startEpoch!==bridge.data().resetEpoch||stopped){
              await games.rpc.request('stop',{lease:result.lease,cancelDownloads:true});
              throw Error('The console session changed. Select the game again.');
            }
            gameState=result;lease=randomUUID();return result;
          }finally{gameStarting=false;}
        }
        case 'start-storage':
          assert(!gameStarting,'Wait for the game export to start.');
          if(gameState){await games.rpc.request('stop',{lease:gameState.lease});gameState=null;lease=randomUUID();}
          if(stopped){stopped=false;lease=randomUUID();}
          return state(await available());
        case 'stop':
          stopped=true;await reset();return {ok:true};
        case 'browse': return browse(target??'/');
        case 'stat': return stat(target);
        case 'sd-upload-begin': {
          ensureLease(args.lease);
          const isSd=onSd(target);
          const name=isSd?sdPath(target):queueName(target);
          assert(Number.isSafeInteger(args.size)&&args.size>=(isSd?0:1),'Invalid upload size.');
          if(!isSd){
            const maximum=/\.(?:iza|izapp)$/i.test(name)?MAX_IMAGE:MAX_PACKAGE;
            assert(args.size<=maximum,'The application package is too large.');
          }
          assert(!upload,'Another file is uploading.');
          const item={name,size:args.size,token:randomUUID(),lease:args.lease,length:0,
            controller:new AbortController(),verify:args.verifySha256===true};
          upload=item;
          item.timer=setTimeout(()=>{
            if(upload!==item)return;
            void cancel(item,'No upload progress for 60 seconds.').catch(error=>console.error(error.message))
              .finally(()=>{if(upload===item)upload=null;});
          },60000);
          item.timer.unref?.();
          try{
            if(isSd){
              assert(ftp&&await available(),ftp?.problem?.()||'Enable IZ2S FTP to write files.');
              ensureCurrent(item);
              assert(name!=='/'&&!await ftp.callWithSignal(item.controller.signal,'stat',name),'The destination already exists.');
              ensureCurrent(item);
              item.stage=path.posix.join(path.posix.dirname(name),'.iz2s-browser-'+randomUUID().replaceAll('-',''));
              item.pipe=ftp.upload(item.stage,item.size,item.controller.signal);
              item.hash=createHash('sha256');
              void item.pipe.closed.catch(error=>{
                if(upload!==item||item.finishing)return;
                item.error=error;
              });
            }else item.bytes=Buffer.allocUnsafe(item.size);
            return {token:item.token,chunkBytes:CHUNK};
          }catch(error){
            try{await cancel(item,error.message);}
            finally{if(upload===item)upload=null;}
            throw error;
          }
        }
        case 'sd-upload-chunk': {
          const item=draft(args.token);
          ensureCurrent(item);
          if(item.error)throw item.error;
          assert(typeof args.data==='string'&&args.data.length<=Math.ceil(CHUNK/3)*4,'Invalid upload chunk.');
          const bytes=Buffer.from(args.data,'base64');
          assert(bytes.length&&args.offset===item.length&&item.length+bytes.length<=item.size,'Unexpected upload offset.');
          assert(!item.writing&&!item.finishing,'Wait for the previous upload chunk.');
          item.writing=true;
          try{
            if(item.pipe){await item.pipe.write(bytes);item.hash.update(bytes);}
            else bytes.copy(item.bytes,item.length);
            ensureCurrent(item);
            item.length+=bytes.length;item.timer.refresh();
          }finally{item.writing=false;}
          return {bytes:item.length};
        }
        case 'sd-upload-finish': {
          const item=draft(args.token);
          ensureCurrent(item);
          assert(!item.writing&&!item.finishing,'Wait for the previous upload operation.');
          assert.equal(item.length,item.size,'The upload is incomplete.');
          clearTimeout(item.timer);
          item.finishing=(async()=>{
            let published=false;
            try{
              if(item.pipe){
                const result=await publish(item);
                published=result.ok;
                return result;
              }
              return await bridge.queuePackage(item.bytes,{canPublish:()=>upload===item&&
                !item.controller.signal.aborted&&bridge.data().resetEpoch===resetEpoch});
            }finally{
              try{if(!published)await discard(item);}
              catch(error){console.error('Could not clean FTP staging file:',error.message);}
              if(upload===item)upload=null;
            }
          })();
          return item.finishing;
        }
        case 'sd-upload-cancel': {
          const item=upload;
          if(!item||item.token!==args.token)return {ok:true};
          try{await cancel(item,'Upload cancelled.');}
          finally{if(upload===item)upload=null;}
          return {ok:true};
        }
        case 'sd-mkdir':
          await write(args,'mkdir',sdPath(target),...(args.parents?['parents']:[]));return {ok:true};
        case 'sd-delete':
          ensureLease(args.lease);
          assert(args.confirmed,'Confirm package removal.');
          if(onSd(target)){await write(args,'delete',sdPath(target));return {ok:true};}
          await bridge.cancelPackage(queueName(target));return {ok:true};
        case 'sd-rename':
          ensureLease(args.lease);
          if(onSd(target)){
            await write(args,'rename',sdPath(target),sdPath(virtualPath('/',args.to)));return {ok:true};
          }
          await bridge.renamePackage(queueName(target),queueName(virtualPath('/',args.to)));return {ok:true};
        case 'retain': {
          ensureLease(args.lease);
          assert(downloads.size<16,'Too many downloads.');
          const token=randomUUID();
          downloads.set(token,{target,file:null});
          try{
            const info=await stat(target);
            ensureLease(args.lease);
            return {token,stat:info,chunkBytes:CHUNK};
          }catch(error){downloads.delete(token);throw error;}
        }
        case 'download-list':
          assert(inside(download(args.token).target,target),'File is outside the download folder.');
          return browse(target);
        case 'download-open': {
          const item=download(args.token);
          assert(inside(item.target,target),'File is outside the download folder.');
          const info=await stat(target);
          assert.equal(info.type,'file','Select a file.');
          closeDownload(item);
          if(info.size&&onSd(target)&&await available()){
            assert(downloads.get(args.token)===item,'The download has expired.');
            item.controller=new AbortController();
            item.pipe=ftp.download(sdPath(target),info.size,item.controller.signal);
          }
          assert(downloads.get(args.token)===item,'The download has expired.');
          item.file=target;
          return info;
        }
        case 'download-read': {
          const {file,pipe}=download(args.token);
          assert(file&&Number.isSafeInteger(args.offset)&&args.offset>=0&&Number.isInteger(args.length)
            &&args.length>0&&args.length<=CHUNK,'Invalid download offset or length.');
          let bytes;
          if(file.startsWith('/apps/')){
            const item=bridge.queuedPackage();
            assert(item&&item.name===queueName(file)&&args.offset+args.length<=item.bytes.length,'Queued package changed.');
            bytes=item.bytes.subarray(args.offset,args.offset+args.length);
          }else if(pipe){
            bytes=await pipe.read(args.offset,args.length);
          }else bytes=await transfer('R',file,args.offset,args.length);
          return {data:bytes.toString('base64')};
        }
        case 'download-close': {
          const item=download(args.token);
          closeDownload(item);return {ok:true};
        }
        case 'download-touch': download(args.token);return {ok:true};
        case 'release': closeDownload(downloads.get(args.token));downloads.delete(args.token);return {ok:true};
        default: throw Error('This action is unavailable for the IZ2S connection.');
      }
    },
  };
}

function start(bridge,folder,addresses){
  const fallback=Array.isArray(addresses)?addresses:[addresses];
  const ftp=require('./paired-sd.cjs').create(folder,()=>[
    bridge.data().consoleAddress,...(typeof addresses==='function'?addresses():fallback),
  ]);
  const backend=create(bridge,ftp);
  bridge.useFtp(ftp,backend.busy);
  return require('../../../tools/fs-rpc-browser/server.cjs').create({
    backend,discover:backend.discover,settingsFile:path.join(folder,'super-file-bros.json'),
  });
}

let directSession;
let connectingDirect=false;
async function openFtp(root,{host},{panelUrl}={}){
  const {privateIP}=require('../../../core/session/network-discovery.cjs');
  assert(typeof host==='string'&&privateIP(host),'Enter the local IPv4 address shown by IZ2S.');
  assert(!connectingDirect&&!directSession?.backend.busy(),'Wait for the current FTP operation to finish.');
  connectingDirect=true;
  let opened;
  try{
    const ftp=require('./paired-sd.cjs').create(null,host);
    assert(await ftp.online(),ftp.problem()||'Enable FTP with L + R3 in IZ2S and check the address.');
    // A failed login leaves the previous local window intact.
    if(directSession){
      assert(!directSession.backend.busy(),'Wait for the current FTP operation to finish.');
      await directSession.backend.request('stop');
      await directSession.app.close();
    }
    const bridge={data:()=>({consoleAddress:host,resetEpoch:0}),files:()=>{
      throw Error(ftp.problem()||'The FTP session has ended. Enable it again on Switch 2.');
    }};
    const backend=create(bridge,ftp,{directFtp:true,games:{
      reset(){},list:()=>({games:[],gamesAvailable:false,gameExportAvailable:false}),
    }});
    const app=opened=await require('../../../tools/fs-rpc-browser/server.cjs').create({
      backend,discover:backend.discover,panelUrl,settingsFile:path.join(root,'data/state/ftp-files.json'),
    });
    app.server.once('close',()=>{
      if(directSession?.app===app)directSession=undefined;
    });
    const tab=await require('../../../core/workspace.cjs').register(root,{
      id:'ftp-files',kind:'files',title:'Super File Bros.',url:app.url,ownerPid:process.pid,
    });
    directSession={app,backend};
    return {tab};
  }catch(error){
    await opened?.close();
    throw error;
  }finally{connectingDirect=false;}
}

module.exports={create,start,rows,openFtp};
