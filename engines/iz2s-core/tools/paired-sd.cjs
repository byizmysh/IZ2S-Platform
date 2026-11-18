'use strict';

const path=require('node:path');
const {spawn}=require('node:child_process');
const {finished}=require('node:stream/promises');
const {isIPv4}=require('node:net');

function create(folder,hosts){
  const candidates=()=>[...new Set((typeof hosts==='function'?hosts():[hosts]).filter(isIPv4))];
  let selected,probed='',checking,probeAt=0;
  const address=()=>candidates().includes(selected)?selected:candidates()[0];
  function processFor(operation,pathname,args,{signal,keepOutput=false,timeout=60000,host=address()}={}){
    signal?.throwIfAborted();
    if(!host)throw Error('No Switch IPv4 address. Check the console network settings.');
    const child=spawn(require('../../../core/platform/python.cjs').command(),[
      '-B',path.join(__dirname,'paired-sd.py'),path.join(folder,'pc-link.key'),host,operation,pathname,...args,
    ],{windowsHide:true,stdio:['pipe','pipe','pipe']});
    let diagnostics='', output='', error;
    const cancel=()=>{error=signal?.reason??Error('FTP transfer cancelled.');child.kill();};
    const timer=setTimeout(()=>{error=Error('No FTP progress before the transfer deadline.');child.kill();},timeout);
    signal?.addEventListener('abort',cancel,{once:true});
    child.stdin.on('error',()=>{});
    child.stderr.on('data',chunk=>{
      timer.refresh();
      diagnostics=(diagnostics+chunk.toString().replace(/^\+\r?\n/gm,'')).slice(-1024);
    });
    if(!keepOutput)child.stdout.on('data',chunk=>{
      timer.refresh();
      output+=chunk;
      if(output.length>4*1024*1024){error=Error('FTP response is too large.');child.kill();}
    });
    const done=new Promise((resolve,reject)=>{
      child.on('error',cause=>{error=cause;});
      child.once('close',code=>{
        clearTimeout(timer);
        signal?.removeEventListener('abort',cancel);
        if(error)return reject(error);
        if(code!==0)return reject(Error(diagnostics.trim()||'Enable the paired IZ2S FTP service with L3 + R3 and retry.'));
        try{resolve(keepOutput?undefined:JSON.parse(output));}catch(cause){reject(cause);}
      });
    });
    void done.catch(()=>{});
    return {child,done,touch:()=>timer.refresh()};
  }
  function call(operation,pathname,...args){
    return callWithSignal(undefined,operation,pathname,...args);
  }
  function callWithSignal(signal,operation,pathname,...args){
    const task=processFor(operation,pathname,args,{signal,timeout:operation==='probe'?3500:60000});
    task.child.stdin.end();
    return task.done;
  }
  async function online(){
    const list=candidates(), fingerprint=list.join(',');
    if(fingerprint===probed&&Date.now()-probeAt<5000)return !!selected;
    if(checking?.fingerprint===fingerprint)return checking.result;
    const job={fingerprint};
    checking=job;
    job.result=(async()=>{
      let reachable;
      for(const host of list){
        const probe=processFor('probe','/',[],{host,timeout:3500});
        probe.child.stdin.end();
        if(await probe.done.then(()=>true,()=>false)){reachable=host;break;}
      }
      if(checking!==job||candidates().join(',')!==fingerprint)return false;
      selected=reachable;probed=fingerprint;probeAt=Date.now();
      return !!reachable;
    })().finally(()=>{if(checking===job)checking=null;});
    return job.result;
  }
  function upload(pathname,size,signal){
    const task=processFor('upload-stream',pathname,[String(size)],{signal});
    const stream=task.child.stdin;
    return {
      async write(bytes){
        signal.throwIfAborted();
        if(stream.destroyed){await task.done;throw Error('FTP input is closed.');}
        await new Promise((resolve,reject)=>stream.write(bytes,error=>error?reject(error):resolve()));
        task.touch();
      },
      async finish(){
        stream.end();
        await Promise.race([finished(stream),task.done]);
        return task.done;
      },
      closed:task.done,
    };
  }
  function download(pathname,size,signal){
    const task=processFor('download-stream',pathname,[String(size)],{signal,keepOutput:true});
    task.child.stdin.end();
    const stream=task.child.stdout;
    const iterator=stream[Symbol.asyncIterator]();
    let remainder=Buffer.alloc(0),offset=0;
    return {
      async read(start,length){
        signal.throwIfAborted();
        if(start!==offset||start+length>size)throw Error('Invalid FTP download range.');
        const chunks=[];
        let pending=length;
        while(pending){
          if(!remainder.length){
            const next=await iterator.next();
            if(next.done){await task.done;throw Error('FTP download was incomplete.');}
            remainder=next.value;
          }
          const count=Math.min(pending,remainder.length);
          chunks.push(remainder.subarray(0,count));
          remainder=remainder.subarray(count);
          pending-=count;
          task.touch();
        }
        offset+=length;
        if(offset===size){
          const next=await iterator.next();
          if(remainder.length||!next.done)throw Error('The file changed during FTP download.');
          await task.done;
        }
        return Buffer.concat(chunks,length);
      },
      closed:task.done,
    };
  }
  return {call,callWithSignal,online,upload,download,address};
}

module.exports={create};
