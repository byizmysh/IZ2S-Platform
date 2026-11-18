'use strict';
const fs = require('node:fs/promises');
const path = require('node:path');
const assert = require('node:assert/strict');
const {createHash, createHmac, randomBytes, timingSafeEqual} = require('node:crypto');
const net = require('node:net');
const http = require('node:http');
const {constants:{errno},networkInterfaces} = require('node:os');
const runtime = require('../runtime.cjs');
const archive = require('../../../core/archive.cjs');
const compat = require('../sd-compat.cjs');
const {prepare,prepareBundle,bundleLauncher,packageModule,portableApp,MAX_IMAGE} = require('./prepare-sd-app.cjs');
const {readState,writeState} = require('./pc-link-state.cjs');
const streams = require('./pc-stream.cjs');
const menuRelease = require('./menu-release.cjs');
const versions = require('./installed-versions.cjs');

const DEFAULT_FOLDER = path.join(runtime.logs(), 'pc-link');
const MAX_PACKAGE = archive.MAX_ARCHIVE;
const LEGACY_TRANSFER_LIMIT = 32 * 1024 * 1024;
const MAX_FILE_REPLY = 256 * 1024;
const IMPORT_UPDATED = 0x495a0002;
const DELETE_REQUEST = 'D'.charCodeAt(0);
const DELETE_TIMEOUT = 30000;
const STORE_ID_BYTES = 48;
const STORE_NAME_BYTES = 64;
const STORE_LIMIT = 64;
const MENU_REPORT_BYTES = 45;
const MENU_ID_BYTES = 16;
const INTERRUPTED_MENU_TRIAL = 0xea13;
const MENU_FAILURE_LIMIT = 16;
const MENU_VERSION_ID = /^[a-f0-9]{32}$/;
const PLATFORM_ROOT = path.resolve(__dirname,'../../..');

async function storeCatalog(){
  const {modules}=await require('../../../core/modules.cjs').list(PLATFORM_ROOT);
  const {visible}=require('../../../core/packages/store-catalog.cjs');
  return modules.filter(item=>{
    if(!visible(item.descriptor)||!item.descriptor.switch||!(/^[a-z0-9][a-z0-9-]{0,46}$/.test(item.id)))return false;
    try{
      const {manifest,files}=archive.switchApp(item);
      return compat.eligibility(manifest,item.descriptor,files,{sha256:item.sha256,archiveFiles:item.files}).compatible;
    }catch{return false;}
  }).sort((a,b)=>a.descriptor.name.localeCompare(b.descriptor.name)).slice(0,STORE_LIMIT);
}

function storeRows(apps){
  const buf=Buffer.alloc(apps.length*(STORE_ID_BYTES+STORE_NAME_BYTES));
  apps.forEach((item,i)=>{
    const at=i*(STORE_ID_BYTES+STORE_NAME_BYTES);
    buf.write(item.id,at,STORE_ID_BYTES-1);
    const name = item.descriptor.name.replace(/[\x00-\x1f\x7f]/g,' ');
    const version = item.descriptor.version;
    if(typeof version==='string'&&/^[a-z0-9][a-z0-9.+_-]{0,13}$/i.test(version)){
      // The original client reads a C string. New consoles also read
      // a tagged version from the reserved tail of the same wire row.
      buf.write(name,at+STORE_ID_BYTES,46);
      if(item.executableInstalled)buf[at+STORE_ID_BYTES+47]=1;
      buf[at+STORE_ID_BYTES+48]=31;
      buf.write(version,at+STORE_ID_BYTES+49,14);
    }else buf.write(name,at+STORE_ID_BYTES,STORE_NAME_BYTES-1);
  });
  return buf;
}
async function keyFile(folder) {
  await fs.mkdir(folder,{recursive:true,mode:0o700});
  const file = path.join(folder,'pc-link.key');
  try { await fs.writeFile(file,randomBytes(32),{flag:'wx',mode:0o600}); }
  catch (error) { if(error.code!=='EEXIST')throw error; }
  const secret = await fs.readFile(file);
  assert.equal(secret.length,32,'Invalid local PC link key');
  return {file,secret};
}

async function prepareDelivery(image,bundleFiles) {
  const temporary = path.join(runtime.output(), 'tmp');
  await fs.mkdir(temporary, {recursive:true});
  const directory = await fs.mkdtemp(path.join(temporary, 'pc-link-iza-'));
  try {
    if(bundleFiles)return await portableApp(await prepareBundle(bundleFiles,directory));
    const input=path.join(directory,'upload.iza');
    await fs.writeFile(input,image,{flag:'wx'});
    return await portableApp(await prepare(input,directory));
  } finally {
    await fs.rm(directory,{recursive:true,force:true});
  }
}

function bridge({secret,pcIp,pcAddresses=[pcIp],consolePort=39524,browserPort=39525,stateFile,parentOrigin,
  readMenuRelease=menuRelease.release,vacooSource,blitSource}) {
  let queued=null, delivered=null, imported=null, lastSeen=0, filesSeen=0, uploading=false, filesPage=null;
  let consoleAddress=null, pcAddress=null, listenerTimer, refreshingListeners=false, stopping=false;
  const listeners=new Map();
  let requiresUpdatedLauncher=false,resetEpoch=0,preparingApp=false,storageBusy=()=>false;
  let stateChanges=Promise.resolve();
  let menuLocal=null,menuOffer=null,menuOffering=false,menuPeer=null,menuOutcome=null;
  let menuRefreshAt=0,menuRefresh=null;
  const media = {vacootube:vacooSource,blitbun:blitSource};
  const resetListeners=new Set();
  const inventoryDigests=new Map();
  let inventoryScan=null,ftp=null,receipts=[];
  const receiptFile=stateFile&&path.join(path.dirname(stateFile),'installed-versions.json');
  let receiptWrites=Promise.resolve();
  function remember(item){
    if(!item.executable||!item.version)return;
    receipts=[{id:item.id,version:item.version,sourceSha256:item.sourceSha256,...item.executable},
      ...receipts.filter(record=>record.id!==item.id||record.sha256!==item.executable.sha256)].slice(0,128);
    if(!receiptFile)return;
    const saved=receipts;
    receiptWrites=receiptWrites.then(()=>require('../../../core/packages/files.cjs').write(receiptFile,saved))
      .catch(error=>console.error('Could not save application version receipt:',error.message));
    return receiptWrites;
  }
  function invalidateDigests(){inventoryDigests.clear();}

  const failedTrials = new Map();
  const menuFailureFile = stateFile && path.join(path.dirname(stateFile), 'menu-failures.json');
  let failureWrites = Promise.resolve();
  const trialKey = (active, target) => active + ':' + target;
  function recordInterruptedTrial(offer, instanceChangedAt, code=INTERRUPTED_MENU_TRIAL) {
    const key = trialKey(offer.previous, offer.id);
    const retryUsed=!!(offer.retry || failedTrials.get(key)?.retryUsed);
    failedTrials.delete(key);
    failedTrials.set(key, {previous:offer.previous, target:offer.id, stage:offer.lastStage, at:instanceChangedAt, retryUsed, code});
    if(failedTrials.size > MENU_FAILURE_LIMIT)failedTrials.delete(failedTrials.keys().next().value);
    if(!menuFailureFile)return;
    const records = JSON.stringify([...failedTrials.values()], null, 2) + '\n';
    failureWrites = failureWrites.then(async () => {
      const temporary = menuFailureFile + '.' + randomBytes(8).toString('hex') + '.tmp';
      try {
        await fs.writeFile(temporary, records, {flag:'wx', mode:0o600});
        await fs.rename(temporary, menuFailureFile);
      } finally {
        await fs.rm(temporary, {force:true});
      }
    }).catch(error => process.stderr.write('Could not save interrupted menu trials: ' + error.message + '\n'));
  }
  async function restoreInterruptedTrials(){
    if(!menuFailureFile)return;
    const rows = await fs.readFile(menuFailureFile, 'utf8').then(JSON.parse).catch(error => {
      if(error.code !== 'ENOENT')process.stderr.write('Could not read interrupted menu trials: ' + error.message + '\n');
      return [];
    });
    if(!Array.isArray(rows))return;
    for(const row of rows.slice(-MENU_FAILURE_LIMIT)){
      if(typeof row?.previous !== 'string' || typeof row.target !== 'string' ||
         !MENU_VERSION_ID.test(row.previous) || !MENU_VERSION_ID.test(row.target) ||
         !['verifying','staged','applying'].includes(row.stage) || !Number.isFinite(Date.parse(row.at)))continue;
      failedTrials.set(trialKey(row.previous, row.target), row);
    }
  }
  const menuTransfers=new Set();
  function refreshMenu(){
    if(menuRefresh)return menuRefresh;
    menuRefreshAt=Date.now();
    menuRefresh=readMenuRelease().then(value=>{menuLocal=value;},()=>{menuLocal=null;})
      .finally(()=>{menuRefresh=null;});
    return menuRefresh;
  }
  const shortVersion = version=>version?.slice(0,12) ?? null;
  function menuStatus(){
    const fresh=menuPeer&&Date.now()-menuPeer.at<10000;
    const active=menuPeer?.active;
    expireMenuOffer();
    const blocked = menuLocal && failedTrials.get(trialKey(active, menuLocal.id));
    return {supported:!!fresh,active:shortVersion(active),available:shortVersion(menuLocal?.id),
      canUpdate:!!(fresh&&menuLocal&&active!==menuLocal.id&&!menuOffer&&!menuOffering&&!blocked),
      canRetry:!!(fresh&&blocked&&!blocked.retryUsed&&!menuOffer&&!menuOffering&&active!==menuLocal.id),
      state:menuOffer?(menuPeer?.target===menuOffer.id&&menuPeer.phase===1?'staged':
        menuPeer?.target===menuOffer.id&&menuPeer.phase===5?'applying':menuTransfers.size?'transferring':
        menuOffer.sends?'verifying':'waiting'):
        blocked?'interrupted':menuOutcome?.state??'idle',
      error:blocked?(blocked.code??INTERRUPTED_MENU_TRIAL):menuOutcome?.error??null,
      interruptedAt:blocked?.stage??menuOutcome?.interruptedAt??null};
  }
  async function offerMenu(retry=false){
    if(!menuStatus().supported)throw Error('Open a version of IZ2S with menu updates enabled.');
    if(menuOffer||menuOffering)throw Error('An IZ2S menu update is already in progress.');
    menuOffering=true;
    try{
      await refreshMenu();
      const release=menuLocal;
      if(!release)throw Error('No compatible IZ2S menu release in this installation.');
      const previous = menuPeer?.active;
      const instance = menuPeer?.instance;
      if(previous===release.id)return {ok:true,current:true};
      const blocked=failedTrials.get(trialKey(previous,release.id));
      if(blocked&&(!retry||blocked.retryUsed))
        throw Error('This menu update was interrupted. The previous version is still active.');
      const bytes=await release.sign(secret);
      if(!menuStatus().supported || menuPeer.active!==previous || menuPeer.instance!==instance)
        throw Error('IZ2S changed sessions while preparing the update.');
      menuOutcome=null;
      menuOffer={id:release.id,previous,bytes,sends:0,offeredAt:Date.now(),lastStage:'verifying',retry:!!blocked};
      if(blocked){
        recordInterruptedTrial(menuOffer,blocked.at,blocked.code);
        await failureWrites;
      }
      return {ok:true,version:shortVersion(release.id)};
    }finally{menuOffering=false;}
  }
  function expireMenuOffer(){
    if(menuOffer&&Date.now()-menuOffer.offeredAt>600000&&!menuTransfers.size&&
       ![1,5].includes(menuPeer?.phase)){
      if(menuOffer.retry)recordInterruptedTrial(menuOffer,new Date().toISOString());
      menuOutcome={state:'expired'};menuOffer=null;
    }
  }
  function updateMenuPeer(report){
    const phase=report[0],active=report.subarray(1,17).toString('hex'),target=report.subarray(17,33).toString('hex');
    if(phase>5)return false;
    const instance=report.subarray(37,45).toString('hex');
    menuPeer={active,target,phase,instance,at:Date.now()};
    if(Date.now()-menuRefreshAt>10000)void refreshMenu();
    if(menuOffer){
      if(active===menuOffer.id){
        menuOutcome={state:'applied'};menuOffer=null;
      }else if(phase===3&&target===menuOffer.id&&menuOffer.sends){
        const error=report.readUInt32BE(33);
        if(menuOffer.retry)recordInterruptedTrial(menuOffer,new Date().toISOString(),error);
        menuOutcome={state:'rejected',error};menuOffer=null;
      }else if(menuOffer.instance&&instance!==menuOffer.instance){
        // The new process reopened the previous menu, without confirming the trial.
        if(active===menuOffer.previous){
          recordInterruptedTrial(menuOffer, new Date().toISOString());
          menuOutcome={state:'interrupted',error:INTERRUPTED_MENU_TRIAL,interruptedAt:menuOffer.lastStage};
        }else menuOutcome={state:'cancelled'};
        menuOffer=null;
      }else if(target===menuOffer.id&&(phase===1||phase===5)){
        menuOffer.lastStage=phase===1?'staged':'applying';
      }else if(menuOffer.sentAt&&Date.now()-menuOffer.sentAt>60000&&target!==menuOffer.id){
        if(menuOffer.retry)recordInterruptedTrial(menuOffer,new Date().toISOString());
      menuOutcome={state:'expired'};menuOffer=null;
      }
    }
    return true;
  }
  const packageTransfers=new Set();
  const video=streams.create();
  const waiting = [];
  const data = () => ({
    online: menuTransfers.size>0 || packageTransfers.size>0 || Date.now()-lastSeen<6500,
    consoleAddress:Date.now()-lastSeen<6500?consoleAddress:null,
    pcAddress:Date.now()-lastSeen<6500?pcAddress:null,
    files: Date.now()-filesSeen<6500,
    queued: queued?.id ?? null,
    delivered,
    imported,
    resetEpoch,
    requiresUpdatedLauncher,
    fileTransfers:uploading||preparingApp||storageBusy(),
    media:Object.fromEntries(Object.entries(media).map(([id,source])=>[id,source?.status?.()])),
    blitbunProfile:video.blitbunProfile(),
    blitbunVideo:video.blitbunVideo(),
    blitbunFps:video.blitbunFps(),
    blitbunTransport:video.blitbunTransport(),
    blitbunAudioBatch:video.blitbunAudioBatch(),
    menu:menuStatus(),
    service:{name:'iz2s-pc-link',version:1,namingVersion:2,pid:process.pid,root:path.resolve(__dirname,'../../..')},
  });
  const mac = (...parts) => {
    const digest = createHmac('sha256',secret);
    for(const part of parts)digest.update(part);
    return digest.digest();
  };
  function changeState(update,canPublish=()=>true) {
    const change=stateChanges.then(async()=>{
      const next=update({queued,delivered,imported});
      if(!next)return;
      assert(canPublish(),'The storage session changed.');
      if(stateFile)await writeState(stateFile,next,canPublish);
      ({queued,delivered,imported}=next);
      if(!queued)requiresUpdatedLauncher=false;
    });
    stateChanges=change.catch(()=>{});
    return change;
  }
  const sockets=new Set();
  async function resetPeer(){
    resetEpoch++;
    invalidateDigests();
    for(const job of waiting.splice(0))job.finish(Error('PC Link session reset.'));
    for(const socket of sockets)socket.destroy();
    lastSeen=filesSeen=0;
    menuPeer=null;
    if(menuOffer)menuOutcome={state:'cancelled'};
    menuOffer=null;
    await Promise.all([...resetListeners].map(listener=>listener()));
  }
  const acceptConsole=socket=>{
    sockets.add(socket);
    let fileJob=null;
    socket.on('close',()=>{
      sockets.delete(socket);
      packageTransfers.delete(socket);
      menuTransfers.delete(socket);
      if(fileJob)fileJob.finish(Error(fileJob.request[0]===DELETE_REQUEST?
        'Подтверждение удаления потеряно. Обновите список приложений.':'Switch closed the file connection.'));
    });
    socket.on('error',()=>socket.destroy());
    socket.setTimeout(3000,()=>socket.destroy());
    const nonce=randomBytes(32);
    socket.write(nonce);
    let pending=Buffer.alloc(0), packageSent=null, authenticated=false, receiptOperation=null, storePending=false;
    socket.on('data', chunk=>{
      if(storePending){socket.destroy();return;}
      if(fileJob){
        if(pending.length+chunk.length>72+MAX_FILE_REPLY){socket.destroy();return;}
        pending=Buffer.concat([pending,chunk]);
        if(pending.length<72)return;
        const header=pending.subarray(0,40);
        const length=header.readUInt32BE(4);
        if(length>MAX_FILE_REPLY||pending.length>72+length||
           !timingSafeEqual(pending.subarray(40,72),mac(nonce,'R',header))){socket.destroy();return;}
        if(pending.length<72+length)return;
        const body=pending.subarray(72);
        if(!timingSafeEqual(createHash('sha256').update(body).digest(),header.subarray(8,40))){
          socket.destroy();return;
        }
        const status=header.readUInt32BE(0);
        fileJob.finish(status?Object.assign(Error('Switch SD error ('+status+').'),{sdResult:status}):null,body);
        fileJob=null;
        socket.end();
        return;
      }
      const operation=String.fromCharCode(pending.length?pending[0]:chunk[0]);
      const streaming=!authenticated&&streams.sources.has(operation);
      const media=streaming&&streams.withAudio.has(operation);
      const controls=streaming&&streams.withInput.has(operation);
      const expected=authenticated?(receiptOperation?36:32):controls?41+streams.inputBytes:media?41:streaming?37:operation==='U'?33+STORE_ID_BYTES:operation==='T'?33+MENU_REPORT_BYTES:operation==='H'?33+MENU_ID_BYTES:33;
      if(pending.length+chunk.length>expected){socket.destroy();return;}
      pending=Buffer.concat([pending,chunk]);
      if(pending.length<expected)return;
      if(authenticated){
        const code=receiptOperation?pending.subarray(0,4):null;
        const signature=code?mac(nonce,receiptOperation,packageSent.digest,code):mac(nonce,'A',packageSent.digest);
        if(!timingSafeEqual(code?pending.subarray(4):pending,signature)){socket.end();return;}
        const id=packageSent.id;
        const result=code?.readUInt32BE();
        const receipt=code?{id,code:result,state:result===0?'installed':
          result===IMPORT_UPDATED?'updated':result===errno.EEXIST?'already-present':'rejected'}:null;
        lastSeen=Date.now();
        void changeState(state=>state.queued?.digest===packageSent.digest?
          {queued:null,delivered:id,imported:receipt}:null)
          .then(async()=>{
            invalidateDigests();
            try{if(result===0||result===IMPORT_UPDATED)await remember(packageSent);}
            finally{socket.end();}
          },error=>{console.error('Could not save PC link receipt:',error.message);socket.destroy();});
        return;
      }
      if((!streaming&&!['F','I','J','K','P','S','C','U','T','H'].includes(operation))||
         !timingSafeEqual(pending.subarray(expected-32),mac(nonce,pending.subarray(0,expected-32)))){
        socket.destroy();return;
      }
      lastSeen=Date.now();
      consoleAddress=socket.remoteAddress;
      pcAddress=socket.localAddress;
      if(operation==='T'||operation==='H'){
        const header=Buffer.alloc(37);
        let body;
        if(operation==='T'){
          if(!updateMenuPeer(pending.subarray(1,1+MENU_REPORT_BYTES))){socket.destroy();return;}
          if(menuOffer&&!menuOffer.sends&&!menuTransfers.size&&!queued&&!packageTransfers.size&&
             (menuPeer.phase===0||menuPeer.phase===2||menuPeer.phase===3)){
            header[0]=4;header.write(menuOffer.id,5,MENU_ID_BYTES,'hex');
          }
        }else if(menuOffer&&!menuOffer.sends&&!menuTransfers.size&&!queued&&!packageTransfers.size&&
                 menuStatus().supported&&pending.subarray(1,1+MENU_ID_BYTES).toString('hex')===menuOffer.id){
          body=menuOffer.bytes;
          header[0]=1;header.writeUInt32BE(body.length,1);
          menuOffer.sends++;menuOffer.instance=menuPeer.instance;menuOffer.sentAt=Date.now();
          menuTransfers.add(socket);
          socket.setTimeout(90000);
        }
        socket.write(Buffer.concat([header,mac(nonce,header)]));
        socket.end(body);
        return;
      }
      if(operation==='C'||operation==='U'){
        storePending=true;
        const id=pending.subarray(1,1+STORE_ID_BYTES).toString('ascii').replace(/\0+$/,'');
        socket.setTimeout(15000);
        const sendStore=(status,body=Buffer.alloc(0))=>{
          if(socket.destroyed)return;
          const header=Buffer.alloc(37);
          header[0]=status;
          header.writeUInt32BE(body.length,1);
          createHash('sha256').update(body).digest().copy(header,5);
          socket.end(Buffer.concat([header,mac(nonce,header),body]));
        };
        void (async()=>{
          const apps=await storeCatalog();
          if(operation==='C'){
            const deadline=AbortSignal.timeout(1800);
            if(ftp&&await ftp.online()){
              for(const app of apps){
                if(deadline.aborted)break;
                const expected=versions.official.find(row=>row.id===app.id&&row.sourceSha256===app.sha256);
                if(!expected)continue;
                try{
                  const target=installedPath(expected);
                  const info=await ftp.callWithSignal(deadline,'stat',target);
                  if(info?.type==='file'&&info.size===expected.bytes)
                    app.executableInstalled=await ftp.callWithSignal(deadline,'digest',target)===expected.sha256;
                }catch{break;}
              }
            }
            sendStore(0,storeRows(apps));return;
          }
          const app=apps.find(item=>item.id===id);
          if(!app){sendStore(2);return;}
          if(queued||preparingApp){sendStore(3);return;}
          if(socket.destroyed)return;
          await queueApplication(()=>packageModule(app),()=>!socket.destroyed,false);
          sendStore(0);
        })().catch(error=>{
          console.error('PC Link App Store:',error.message);
          sendStore(4);
        });
        return;
      }
      if(streaming){
        const {status,bytes,digest}=video.response(operation,pending.readUInt32BE(1),media?pending.readUInt32BE(5):0,
          controls?pending.subarray(9,9+streams.inputBytes):null);
        const source = streams.sources.get(operation);
        if (source === 'blitbun' && operation !== 'a' && operation !== 'b') blitSource?.request(status !== 0);
        if (source === 'vacootube') vacooSource?.request(status !== 0);
        const header=Buffer.alloc(37);
        header[0]=status;
        if(bytes){header.writeUInt32BE(bytes.length,1);digest.copy(header,5);}
        socket.setNoDelay(true);
        socket.cork();
        socket.write(Buffer.concat([header,mac(nonce,header)]));
        socket.end(bytes);
        return;
      }
      if(operation==='S'){
        filesSeen=lastSeen;
        fileJob=waiting.shift() ?? null;
      }
      const header=Buffer.alloc(37);
      if(operation==='S'&&!fileJob&&queued&&!packageTransfers.size){header[5]=0x49;header[6]=0x5a;}
      if(operation==='J'||operation==='K')receiptOperation=operation;
      const fetching=receiptOperation||operation==='F'||operation==='I';
      if(fetching&&queued&&operation!=='K'&&
         (queued.bytes.length>LEGACY_TRANSFER_LIMIT||(queued.packed&&operation==='F'))){
        requiresUpdatedLauncher=true;
        header[0]=3;
      }else if(fetching&&!packageTransfers.size){
        packageSent=queued;
        if(operation==='K')requiresUpdatedLauncher=false;
      }
      const payload = packageSent?.bytes ?? fileJob?.request;
      if(payload){
        header[0]=fileJob?2:1;
        header.writeUInt32BE(payload.length,1);
        (packageSent?.digest ?? createHash('sha256').update(payload).digest()).copy(header,5);
      }
      // For an empty K reply, the digest field can carry the last delivery outcome.
      // Earlier launchers see the same status and zero payload size.
      if(operation==='K'&&!payload&&delivered){
        const code=imported?.code;
        const confirmed=Number.isInteger(code)&&code>=0&&code<=0xffffffff;
        header[5]=confirmed?1:2;
        if(confirmed)header.writeUInt32BE(code,6);
      }
      const signed=Buffer.concat([header,mac(nonce,header)]);
      if(!payload){socket.end(signed);return;}
      pending=Buffer.alloc(0);
      authenticated=true;
      if(packageSent)packageTransfers.add(socket);
      const timeout=fileJob?.request[0]===DELETE_REQUEST?DELETE_TIMEOUT:2500;
      socket.setTimeout(fileJob?timeout:operation==='K'?360000:receiptOperation?120000:30000);
      socket.write(signed);
      socket.write(payload);
    });
  };

  function files(request){
    assert(data().files,'Open the updated IZ2S on Switch.');
    assert(waiting.length<16,'Too many file requests.');
    assert(Buffer.isBuffer(request)&&request.length>=14&&request.length<=781,'Invalid file request.');
    return new Promise((resolve,reject)=>{
      const job={request,finish(error,body){clearTimeout(timer);error?reject(error):resolve(body);}};
      const timer=setTimeout(()=>{
        const index=waiting.indexOf(job);
        if(index>=0)waiting.splice(index,1);
        const mayHaveRun=request[0]===DELETE_REQUEST&&index<0;
        job.finish(Error(mayHaveRun?'Подтверждение удаления потеряно. Обновите список приложений.':
          'Switch did not answer the file request.'));
      },request[0]===DELETE_REQUEST?DELETE_TIMEOUT:5000);
      waiting.push(job);
    });
  }

  function fileRequest(operation, value, offset=0, length=0) {
    const bytes=Buffer.from(value);
    const request=Buffer.alloc(13+bytes.length);
    request.write(operation);
    request.writeBigUInt64BE(BigInt(offset),1);
    request.writeUInt32BE(length,9);
    bytes.copy(request,13);
    return files(request);
  }

  const installedPath=row=>require('../../../core/module-storage.cjs').paths(row.id).path+'/launcher.iza';
  async function installedDigest(row){
    const target=installedPath(row);
    if(ftp&&await ftp.online())return ftp.call('digest',target);
    const digest=createHash('sha256');
    for(let offset=0;offset<row.bytes;){
      const length=Math.min(row.bytes-offset,MAX_FILE_REPLY);
      const data=await fileRequest('R',target,offset,length);
      assert(data.length===length,'The SD image changed during verification.');
      digest.update(data);
      offset+=length;
    }
    return digest.digest('hex');
  }
  async function installedApps() {
    const {rows}=require('./pc-link-files.cjs');
    const body=await fileRequest('L',require('../../../core/module-storage.cjs').BASE);
    const valid=/^[a-z0-9][a-z0-9-]{0,46}$/;
    const apps=rows(body).flatMap(row=>{
      if(!row.name.endsWith('.nro'))return valid.test(row.name)?[{id:require('../../../core/module-storage.cjs').identity(row.name),bytes:row.size,format:'.iza'}]:[];
      const name=row.name.slice(0,-4);
      if(!valid.test(name))return [];
      const target='/sd/iz2s/modules/'+name+'/launcher.nro';
      return [{id:require('./nro-package.cjs').rowId(target),name,bytes:row.size,format:'.nro',
        displayPath:'SD:'+target.slice(3)}];
    }).sort((a,b)=>a.id.localeCompare(b.id));
    const candidates=apps.filter(row=>row.format==='.iza'&&
      [...receipts,...versions.official].some(record=>record.id===row.id&&record.bytes===row.bytes));
    const key=row=>row.id+':'+row.bytes;
    if(!inventoryScan){
      const epoch=resetEpoch,at=Date.now();
      inventoryScan=(async()=>{
        for(const row of candidates){
          if(epoch!==resetEpoch)break;
          const previous=inventoryDigests.get(key(row));
          if(previous&&at-previous.at<60000)continue;
          try{
            const digest=await installedDigest(row);
            if(epoch===resetEpoch)inventoryDigests.set(key(row),{digest,at:Date.now()});
          }catch{break;}
        }
      })().finally(()=>{inventoryScan=null;});
    }
    return apps.map(row=>{
      const entry=inventoryDigests.get(key(row));
      return versions.label(row,entry?.digest,receipts);
    });
  }

  async function removeApp(req) {
    const size=Number(req.headers['content-length']);
    assert(Number.isInteger(size)&&size>0&&size<=47,'Выберите установленный модуль.');
    let id='';
    for await(const chunk of req){
      id+=chunk.toString('latin1');
      assert(id.length<=size,'Неверный ID модуля.');
    }
    assert(id.length===size&&/^[a-z0-9][a-z0-9-]{0,46}$/.test(id),'Неверный ID модуля.');
    const receipt=await fileRequest('D',id);
    assert(!receipt.length,'Неверное подтверждение удаления.');
    invalidateDigests();
    return {removed:id};
  }

  async function queueApplication(prepareApp, canPublish=()=>true, skipVerifiedDuplicate=true){
    assert(!queued&&!preparingApp,'An app is already being prepared or queued.');
    const epoch=resetEpoch;
    preparingApp=true;
    try{
      const prepared=await prepareApp();
      if(skipVerifiedDuplicate&&!prepared.packed&&prepared.executable&&ftp&&await ftp.online()){
        const row={id:prepared.id,bytes:prepared.executable.bytes};
        const target=installedPath(row);
        const same=await (async()=>{
          const stat=await ftp.call('stat',target);
          return stat?.type==='file'&&stat.size===row.bytes&&
            await ftp.call('digest',target)===prepared.executable.sha256;
        })().catch(()=>false);
        assert(epoch===resetEpoch&&canPublish(),'The request ended before the application was ready.');
        if(same){await remember(prepared);invalidateDigests();return {alreadyInstalled:prepared.id};}
      }
      const item={...prepared,digest:createHash('sha256').update(prepared.bytes).digest()};
      await changeState(state=>{
        assert(!state.queued,'The previous package is still queued.');
        return {...state,queued:item,delivered:null,imported:null};
      },()=>epoch===resetEpoch&&canPublish());
      requiresUpdatedLauncher=false;
      return {queued:prepared.id};
    }finally{preparingApp=false;}
  }

  function queuePackage(bytes,{canPublish}={}){
    return queueApplication(async()=>{
      assert(bytes.length&&bytes.length<=MAX_IMAGE,'Application exceeds the 32-bit transfer format.');
      if(bytes.subarray(0,8).toString('ascii')==='IZSDAPP1'){
        assert(bytes.length<=MAX_IMAGE,'IZA exceeds the 32-bit executable format.');
        return prepareDelivery(bytes);
      }
      const entries=archive.decode(bytes,{allowDirectories:true});
      if(entries.some(file=>file.name==='module.json')){
        const checked={...archive.inspectFiles(entries),sha256:archive.sha(bytes),packageBytes:bytes.length};
        return packageModule(checked);
      }
      const image=bundleLauncher(entries);
      assert(image,'Include launcher.iza in the SD bundle.');
      return prepareDelivery(image.bytes,entries);
    },canPublish);
  }

  async function queue(req,res) {
    const declared=Number(req.headers['content-length']);
    assert(!uploading,'Another upload is in progress.');
    assert(Number.isInteger(declared)&&declared>0&&declared<=MAX_PACKAGE,'The application file is empty or too large.');
    uploading=true;
    try {
      const payload=Buffer.allocUnsafe(declared);
      let length=0;
      for await(const chunk of req){
        assert(chunk.length<=declared-length,'Invalid upload length.');
        chunk.copy(payload,length);
        length+=chunk.length;
      }
      assert.equal(length,declared);
      return await queuePackage(payload,{canPublish:()=>!res.destroyed});
    } finally { uploading=false; }
  }

  async function commandBody(req){
    let body='';
    for await(const chunk of req){
      body+=chunk;
      assert(body.length<=1024,'Command is too large.');
    }
    const parsed=body?JSON.parse(body):{};
    assert(parsed&&typeof parsed==='object'&&!Array.isArray(parsed),'Invalid command.');
    return parsed;
  }

  const browser=http.createServer({requestTimeout:30000},async (req,res)=>{
    const host='127.0.0.1:'+browser.address().port;
    function reply(status,type,value){
      res.writeHead(status,{'content-type':type,'cache-control':'no-store',
        'x-content-type-options':'nosniff','content-security-policy':
        "default-src 'none'; script-src 'self'; style-src 'unsafe-inline'; connect-src 'self'; form-action 'self'; frame-ancestors 'none'"});
      res.end(value);
    }
    const json=(status,value)=>reply(status,'application/json; charset=utf-8',JSON.stringify(value));
    if(req.headers.host!==host){json(403,{error:'Unexpected host.'});return;}
    if(req.method==='GET'&&req.url==='/status')return json(200,data());
    const mediaAction=req.method==='POST'&&/^\/media\/(vacootube|blitbun)\/(start|stop)$/.exec(req.url);
    if(mediaAction&&media[mediaAction[1]]){
      if(req.headers.origin!=='http://'+host)return json(403,{error:'Use the local platform to control PC video.'});
      const source=media[mediaAction[1]];
      try{
        const body=await commandBody(req);
        return json(200,mediaAction[2]==='start'?await source.start(body.window):source.stop());
      }catch(error){return json(409,{error:error.message});}
    }
    if(req.method==='POST'&&req.url==='/menu-update'){
      if(req.headers.origin!=='http://'+host||req.headers['content-type']!=='application/json')
        return json(403,{error:'Use the local platform to update IZ2S.'});
      try{return json(200,await offerMenu((await commandBody(req)).retry===true));}
      catch(error){return json(409,{error:error.message});}
    }
    if(req.method==='POST'&&req.url==='/reset'){
      if(req.headers.origin!=='http://'+host||req.headers['content-type']!=='application/json')
        return json(403,{error:'Use the local platform to reset the session.'});
      if(menuOffering||menuOffer&&['transferring','staged','applying','verifying'].includes(menuStatus().state))
        return json(409,{error:'Wait for the IZ2S menu update to finish.'});
      try {await resetPeer();return json(200,{ok:true});}
      catch(error){return json(409,{error:error.message});}
    }
    if(req.method==='GET'&&req.url==='/diagnostics'){
      try {return json(200,JSON.parse((await fileRequest('Q','/')).toString('utf8')));}
      catch(error){return json(503,{error:error.message});}
    }
    if(req.method==='GET'&&req.url==='/installed'){
      try {return json(200,{apps:await installedApps()});}
      catch(error) {
        return json(400,{error:error.sdResult===errno.EINVAL?
          'Обновите IZ2S, чтобы управлять установленными приложениями.':error.message});
      }
    }
    if(req.method==='POST'&&req.url==='/installed/remove'){
      if(req.headers.origin!=='http://'+host||!/^text\/plain(?:;\s*charset=UTF-8)?$/i.test(req.headers['content-type']||''))
        return json(403,{error:'Откройте управление приложениями на этой странице.'});
      try {return json(200,await removeApp(req));}
      catch(error) {
        return json(error.sdResult===errno.EBUSY?409:400,{error:error.sdResult===errno.EBUSY?
          'Вернитесь в главное меню IZ2S и повторите удаление.':error.message});
      }
    }
    if(req.url?.startsWith('/stream/'))return video.upload(req,host,json);
    if(req.method==='GET'&&req.url==='/files'&&filesPage){
      res.writeHead(302,{location:filesPage,'cache-control':'no-store','referrer-policy':'no-referrer'});
      res.end();
      return;
    }
    if(req.method==='POST'&&req.url==='/upload'){
      if(req.headers.origin!=='http://'+host||req.headers['content-type']!=='application/octet-stream'){
        json(403,{error:'Choose a package on this page.'});return;
      }
      try { json(200,await queue(req,res)); }
      catch (error) { json(400,{error:error.message}); }
      return;
    }
    json(404,{error:'Unknown request.'});
  });
  require('../../../core/panel-embedding.cjs').allow(browser,parentOrigin);
  const listen=(server,options)=>new Promise((resolve,reject)=>{
    server.once('error',reject);
    server.listen(options,()=>{server.off('error',reject);resolve(server.address());});
  });
  return {
    data, files, queuePackage,
    resetStorage:resetPeer,
    useFtp(storage,busy){ftp=storage;storageBusy=busy;},
    onReset(listener){resetListeners.add(listener);},
    openFilesAt(url){filesPage=url;},
    queuedPackage:()=>queued,
    cancelPackage(name){
      return changeState(state=>{
        assert(state.queued?.name===name,'Select the queued package.');
        return {...state,queued:null};
      });
    },
    renamePackage(previousName,name){
      return changeState(state=>{
        assert(state.queued?.name===previousName,'Select the queued package.');
        return {...state,queued:{...state.queued,name}};
      });
    },
    async start(){
      await restoreInterruptedTrials();
      await refreshMenu();
      if(stateFile)({queued=null,delivered=null,imported=null}=await readState(stateFile,MAX_PACKAGE));
      if(receiptFile){
        const saved=await require('../../../core/packages/files.cjs').read(receiptFile).catch(error=>{
          console.error('Could not read supported application version receipts:',error.message);
          return null;
        });
        if(Array.isArray(saved))receipts=saved.filter(row=>typeof row?.version==='string'&&/^[a-f0-9]{64}$/.test(row.sha256)).slice(0,128);
      }
      await refreshListeners();
      listenerTimer=setInterval(()=>void refreshListeners().catch(error=>console.error('PC Link interfaces:',error.message)),3000);
      listenerTimer.unref();
      const web=await listen(browser,{host:'127.0.0.1',port:browserPort});
      return {socket:listeners.values().next().value?.address()??null,browser:web};
    },
    stop(){
      stopping=true;
      clearInterval(listenerTimer);
      for(const source of Object.values(media))source?.close();
      for(const job of waiting.splice(0))job.finish(Error('PC link is shutting down.'));
      for(const socket of sockets)socket.destroy();
      for(const server of listeners.values())server.close();
      listeners.clear();
      browser.close();
    },
  };
  async function refreshListeners(){
    if(refreshingListeners||stopping)return;
    refreshingListeners=true;
    try{
      const available=new Set(Object.values(networkInterfaces()).flat().map(row=>row.address));
      const addresses=typeof pcAddresses==='function'?await pcAddresses():pcAddresses;
      const wanted=new Set(addresses.filter(ip=>available.has(ip)||ip.startsWith('127.')));
      for(const [ip,server] of listeners){
        if(wanted.has(ip))continue;
        listeners.delete(ip);
        server.close();
      }
      const port=consolePort||listeners.values().next().value?.address()?.port||0;
      for(const ip of wanted){
        if(stopping||listeners.has(ip))continue;
        const server=net.createServer(acceptConsole);
        try{
          await listen(server,{host:ip,port:port||listeners.values().next().value?.address()?.port||0});
          if(stopping){server.close();break;}
          listeners.set(ip,server);
        }catch(error){
          if(error.code!=='EADDRNOTAVAIL'&&error.code!=='EADDRINUSE')console.error('PC Link listener:',error.message);
        }
      }
    }finally{refreshingListeners=false;}
  }
}

async function main(){
  await require('../../../core/platform/core-migration.cjs').migrate(PLATFORM_ROOT);
  const {secret}=await keyFile(DEFAULT_FOLDER);
  async function readConnection(){
    const file=path.join(DEFAULT_FOLDER,'connection.json');
    const config=await fs.readFile(file,'utf8').then(JSON.parse).catch(error=>{
      if(error.code==='ENOENT')return {pcIp:'10.42.0.1',consoleIp:'10.42.0.20'};
      throw error;
    });
    assert(net.isIPv4(config.pcIp)&&net.isIPv4(config.consoleIp),'Invalid PC Link addresses');
    const pcAddresses=config.pcAddresses??[config.pcIp];
    const consoleAddresses=config.consoleAddresses??[config.consoleIp];
    assert(Array.isArray(pcAddresses)&&pcAddresses.length<=8&&pcAddresses.every(net.isIPv4),'Invalid PC Link host list');
    assert(Array.isArray(consoleAddresses)&&consoleAddresses.length<=2&&consoleAddresses.every(net.isIPv4),'Invalid Switch address list');
    return {pcAddresses,consoleAddresses};
  }
  let connection=await readConnection();
  const instance=bridge({secret,pcAddresses:async()=>{
    connection=await readConnection();
    return connection.pcAddresses;
  },
    vacooSource:require('./pc-media-control.cjs').vacooDemand(),
    blitSource:require('./pc-media-control.cjs').sourceDemand('blitbun'),
    stateFile:path.join(DEFAULT_FOLDER,'queue.state'),
    parentOrigin:await require('../../../core/panel-embedding.cjs').origin(path.resolve(__dirname,'../../..'))});
  try {
    const address=await instance.start();
    const fileServer=await require('./pc-link-files.cjs').start(instance,DEFAULT_FOLDER,()=>connection.consoleAddresses);
    instance.openFilesAt(fileServer.url);
    process.stdout.write(JSON.stringify(address)+'\n');
    for(const signal of ['SIGTERM','SIGINT'])process.once(signal,()=>{
      void instance.resetStorage().catch(error=>console.error(error.message)).finally(()=>{
        instance.stop();
        fileServer.close();
      });
    });
  } catch(error) { instance.stop(); throw error; }
}

if(require.main===module)main().catch(error=>{console.error(error.message);process.exitCode=1;});
module.exports={bridge,keyFile,DEFAULT_FOLDER,MAX_IMAGE,MAX_PACKAGE};
