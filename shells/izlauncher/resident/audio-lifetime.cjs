'use strict';
// Startup/close coordination at existing stopped-render boundaries only.
// During play the unchanged finite owner makes no AudioOut calls or PCM copies.
const check=(v,m)=>{if(!v)throw Error(m);};
async function create({rpc,slots,root,createOwner}){
 const selected=slots.filter(s=>s.service===1);check(selected.length<=1,'Only one resident finite audio output is supported');
 const owners=[];
 for(const slot of selected){
  require('./services.cjs').policy(slot.app);
  const context={rpc,app:slot.app,mapped:slot.controls.mapped,directory:slot.appDirectory,sdStorage:require('../../../core/module-storage.cjs').forModule(slot.app.id)};
  const load=createOwner||async function(c){
   const component=await require('../../../core/components.cjs').create(root).get(c.app.id,'player');
   return component.create(c);
  };
  const owner=await load(context);
  check(owner&&['pending','afterBatch','close','state'].every(k=>typeof owner[k]==='function'),'Finite audio owner interface differs');
  owners.push({slot,owner});
 }
 const read=async()=>Promise.all(owners.map(async({slot})=>rpc.read(slot.controls.mapped,1024)));
 const status=()=>({mode:owners.length?'finite-audio':'graphics-only',queueSeconds:owners.length?699.0506666666666:0,
  ready:owners.length>0&&owners.every(({owner})=>{const s=owner.state();return s.started&&!s.closed&&!s.uncertain;}),
  owners:owners.map(({slot,owner})=>({id:slot.id,...owner.state()}))});
 return {
  status,
  async pending(){const states=await read();return owners.some(({owner},i)=>owner.pending(states[i]));},
  async atBoundary({workerStopped=false}={}){
   check(workerStopped,'Resident audio requires a joined render worker');
   const states=await read();for(let i=0;i<owners.length;i++)if(owners[i].owner.pending(states[i]))await owners[i].owner.afterBatch(states[i]);
   return status();
  },
  async close({workerStopped=false}={}){check(workerStopped,'Resident audio retains storage until the render worker is joined');for(const {owner} of owners)await owner.close();return status();}
 };
}
module.exports={create};
