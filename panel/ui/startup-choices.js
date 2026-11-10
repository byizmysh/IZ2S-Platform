'use strict';
// Saved choices are suggestions for this game/menu only; enabling loading is
// deliberately not persisted. Legacy global choices never become selections.
(function(root){
 function scope(state){
  const civ=['civswitch','civswitch-ldn'].includes(state?.platform?.loaderId);
  return (civ?'civ7':'sb4')+':'+(civ?state?.civLauncher?.shell||'izlauncher':state?.platform?.variant||'original');
 }
 function create(preferences){
  const selected=new Set();let current=null;
  function activate(state){
   const next=scope(state);if(next===current)return false;
   current=next;selected.clear();
   try{const ids=JSON.parse(preferences?.getItem('iz2s.startupChoices.v2.'+current)||'[]');if(Array.isArray(ids))for(const id of ids)if(typeof id==='string'&&/^[a-z0-9][a-z0-9-]{0,47}$/.test(id))selected.add(id);}catch{}
   return true;
  }
  function save(){if(!current)return;try{preferences?.setItem('iz2s.startupChoices.v2.'+current,JSON.stringify([...selected]));}catch{}}
  function payload(enabled){return {moduleIds:enabled?[...selected]:[],restoreInstalled:false};}
  return {selected,activate,save,payload};
 }
 const api={scope,create};root.IZ2S_STARTUP=api;if(typeof module==='object'&&module.exports)module.exports=api;
})(globalThis);
