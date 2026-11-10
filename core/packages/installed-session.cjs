'use strict';
// Historical published session set. Never use it as an implicit startup list.
// SD applications have their own on-card inventory and never enter this list.
const fs=require('node:fs/promises'),path=require('node:path');
const files=require('./files.cjs'),{ID}=require('../descriptors.cjs');
function location(root,shell){if(!['izlauncher','izpslauncher'].includes(shell))throw Error('Unknown console shell');return path.join(root,'data/state/installed-session-'+shell+'.json');}
function ids(apps){if(!Array.isArray(apps)||apps.length>15)throw Error('Invalid saved installation list');return [...new Set(apps.map(app=>{const id=typeof app==='string'?app:app?.id;if(typeof id!=='string'||!ID.test(id))throw Error('Invalid saved module identity');return id;}))];}
async function save(root,shell,apps){const value={format:'iz-session-installed-v1',shell,ids:ids(apps),updated:new Date().toISOString()};await files.write(location(root,shell),value);return value.ids;}
async function read(root,shell){
 const value=await files.read(location(root,shell));
 if(value){if(value.format!=='iz-session-installed-v1'||value.shell!==shell)throw Error('Invalid saved installation record');return ids(value.ids);}
 // Migrate installations published by older RUN22 builds. Only a matching,
 // ready menu is evidence; an empty/failed startup must not hide the last set.
 const folder=path.join(root,'data/state/attempts'),runs=path.join(root,'data/sessions');let names;
 try{names=await fs.readdir(folder);}catch(e){if(e.code==='ENOENT')return [];throw e;}
 const rows=[];for(const name of names){if(!/^[a-f0-9-]{36}$/.test(name))continue;const row=await files.read(path.join(folder,name,'attempt.json'));if(row?.kind==='launch'&&row.runDirectory&&Number.isFinite(Date.parse(row.created))&&path.dirname(path.resolve(row.runDirectory))===runs&&/^launcher-[A-Za-z0-9]+$/.test(path.basename(row.runDirectory)))rows.push(row);}
 rows.sort((a,b)=>b.created.localeCompare(a.created));
 for(const row of rows){const menu=await files.read(path.join(row.runDirectory,'run11-runtime.json'));if(menu?.shell?.id!==shell||menu.sd||!(menu.menuReady||menu.completed&&menu.shell?.ready)||!Array.isArray(menu.installed))continue;return save(root,shell,menu.installed);}
 return [];
}
async function selection(root,shell,{moduleIds=[],sd=false}={}){
 // Only the explicit request may populate RAM. Old callers' restoreInstalled
 // flag must not revive a set belonging to another game or previous run.
 return sd?[]:ids(moduleIds);
}

module.exports={read,save,selection};
