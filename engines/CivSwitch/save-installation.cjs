'use strict';
const path=require('node:path');
const {readJson,saveJson,createLedger}=require('../../core/session/ledger.cjs');
const location=root=>path.join(root,'data/state/civ-save-installation.json'),migrations=new Map(),epochs=new Map();
async function begin(root,engine){epochs.set(root,(epochs.get(root)||0)+1);await saveJson(location(root),{state:'installing',saveName:'IZ2S.Civ7Save',engine,startedAt:new Date().toISOString()});}
async function confirm(root,engine){
 epochs.set(root,(epochs.get(root)||0)+1);
 const record={state:'installed',saveName:'IZ2S.Civ7Save',engine,confirmedAt:new Date().toISOString()};
 await saveJson(location(root),record);return record;
}
async function migrate(root,epoch){
 const ledger=createLedger(root),rows=(await ledger.listAttempts()).filter(row=>row.loaderId==='civswitch-ldn'||row.kind==='civswitch-ldn').slice(0,50);
 for(const row of rows){
  const entry=(await ledger.recent(row.id)).find(e=>e.type==='civswitch-ldn'&&e.message==='INSTALL DONE. Restart the game and open Load Game.');
  if(!entry)continue;
  const record={state:'installed',saveName:'IZ2S.Civ7Save',engine:'civswitch-ldn',confirmedAt:entry.at,source:'installation-log'};
  if((epochs.get(root)||0)!==epoch)return {state:'unconfirmed',saveName:'IZ2S.Civ7Save'};
  await saveJson(location(root),record);return record;
 }
 return {state:'unconfirmed',saveName:'IZ2S.Civ7Save'};
}
async function read(root,{phase}={}){
 const record=await readJson(location(root),null).catch(()=>({state:'unconfirmed'}));
 // An incomplete new installation invalidates the earlier confirmation: the
 // old save may already have been deleted. Never recover an older success.
 if(record)return record.state==='installed'?record:{state:'unconfirmed',saveName:'IZ2S.Civ7Save'};
 if(phase==='installed')return {state:'installed',saveName:'IZ2S.Civ7Save'};
 if(!migrations.has(root))migrations.set(root,migrate(root,epochs.get(root)||0).catch(()=>({state:'unconfirmed',saveName:'IZ2S.Civ7Save'})));
 return migrations.get(root);
}
module.exports={begin,confirm,read};
