'use strict';
const assert=require('node:assert/strict');
const MAX=0xffffffffffffffffn;
const id=value=>value.toString(16).padStart(16,'0');
function create(bridge,{rpc=require('../../../tools/dolphin-fs-rpc/control.cjs')}={}){
  let cached=null,pending=null,generation=0;
  const owner=()=>JSON.stringify([bridge.data().resetEpoch,bridge.data().consoleAddress]);
  function reset(){generation++;cached=null;}
  async function request(op,name){
    const text=Buffer.from(name),packet=Buffer.alloc(13+text.length);
    packet[0]=op.charCodeAt(0);text.copy(packet,13);
    return bridge.files(packet);
  }
  async function native(check){
    const read=async(op,name)=>{check();const value=await request(op,name);check();return value;};
    const found=new Map();
    for(const storage of [4,5,2]){
      async function list(low,high){
        const body=await read('G',`/games/${storage}/${id(low)}/${id(high)}`);
        assert(body.length>=8,'Truncated game metadata reply.');
        const total=body.readInt32LE(),count=body.readInt32LE(4);
        assert(count>=0&&count<=32&&total>=count&&body.length===8+count*16,'Invalid game metadata reply.');
        for(let at=8;at<body.length;at+=16){
          const title=body.readBigUInt64LE(at);
          assert(title>=low&&title<=high&&body[at+12]===0x80,'Invalid game metadata key.');
          const key=id(title),game=found.get(key)||{title:key,name:key,storages:[]};
          if(!game.storages.includes(storage))game.storages.push(storage);
          found.set(key,game);
        }
        if(total>count&&low<high){const middle=(low+high)/2n;await list(low,middle);await list(middle+1n,high);}
      }
      try{await list(0n,MAX);}catch(error){
        // RUN22 also skips only absent/inactive storage, never permission errors.
        if(![0x21005,0x20a05].includes(error.sdResult&0x3fffff))throw error;
      }
    }
    const games=[...found.values()];let missingNames=0;
    for(const game of games){
      try{const name=new TextDecoder('utf-8',{fatal:true}).decode(await read('N','/games/'+game.title)).replace(/\0.*$/s,'').trim();if(name)game.name=name;else missingNames++;}
      catch(error){if(!Number.isInteger(error.sdResult))throw error;missingNames++;}
    }
    return {games:games.sort((a,b)=>a.name.localeCompare(b.name)),gamesAvailable:true,
      gamesSource:'iz2s-native',gameExportAvailable:false,missingNames};
  }
  async function list({refresh=false}={}){
    const identity=owner();
    if(!refresh&&cached?.identity===identity&&Date.now()-cached.at<60000)return cached.value;
    if(pending?.identity===identity)return pending.promise;
    const revision=generation,task={identity};
    task.promise=(async()=>{
      let rpcError;
      try{const result=await rpc.request('list',{refresh});return {...result,gamesAvailable:true,gamesSource:'fs-rpc',gameExportAvailable:true};}
      catch(error){rpcError=error.message;}
      if(!bridge.data().files)return {games:[],gamesAvailable:false,gameExportAvailable:false,gamesError:rpcError};
      try{return await native(()=>assert(revision===generation&&identity===owner(),'The console session changed. Refresh the game list.'));}
      catch(error){return {games:[],gamesAvailable:false,gameExportAvailable:false,
        gamesError:error.sdResult===22?'Update IZ2S to read installed games without SmileBASIC.':error.message,
        rpcError};}
    })().then(value=>{
      assert(revision===generation&&identity===owner(),'The console session changed. Refresh the game list.');
      // Errors are not cached: reconnect/retry can recover immediately.
      if(value.gamesAvailable)cached={identity,at:Date.now(),value};
      return value;
    }).finally(()=>{if(pending===task)pending=null;});
    pending=task;return task.promise;
  }
  return {list,reset,rpc};
}
module.exports={create};
