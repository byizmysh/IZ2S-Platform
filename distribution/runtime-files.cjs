'use strict';
const fs=require('node:fs/promises'),path=require('node:path'),crypto=require('node:crypto');
const digest=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
function safe(name){if(typeof name!=='string'||!name||/[\\:\x00-\x1f]/.test(name)||name.split('/').some(x=>!x||x==='.'||x==='..'))throw Error('Invalid runtime inventory path');return name;}
async function inventory(root){const manifest=JSON.parse(await fs.readFile(path.join(root,'distribution/files.json')));if(manifest.format!=='iz2s.runtime-distribution'||manifest.version!==1||!manifest.files||typeof manifest.files!=='object'||Array.isArray(manifest.files))throw Error('Invalid runtime inventory');return manifest;}
async function verify(root){const manifest=await inventory(root);let bytes=0;for(const [name,row] of Object.entries(manifest.files)){
 safe(name);if(!row||!/^[a-f0-9]{64}$/.test(row.sha256)||!Number.isSafeInteger(row.bytes)||typeof row.executable!=='boolean')throw Error('Invalid runtime entry');
 const file=path.join(root,...name.split('/')),stat=await fs.lstat(file);if(!stat.isFile()||stat.isSymbolicLink())throw Error('Invalid runtime file: '+name);
 const content=await fs.readFile(file);if(content.length!==row.bytes||digest(content)!==row.sha256)throw Error('Runtime integrity failure: '+name);bytes+=content.length;
 }return {manifest,files:Object.keys(manifest.files).length,bytes};}
async function copySource(source,destination){
 const relative=path.relative(path.resolve(source),path.resolve(destination));if(!relative||(!relative.startsWith('..'+path.sep)&&relative!=='..'&&!path.isAbsolute(relative)))throw Error('Choose a destination outside the source');
 const {manifest,files}=await verify(source);
 for(const [name,row]of Object.entries(manifest.files)){const target=path.join(destination,...name.split('/'));await fs.mkdir(path.dirname(target),{recursive:true});await fs.copyFile(path.join(source,...name.split('/')),target);await fs.chmod(target,row.executable?0o755:0o644);}
 await fs.copyFile(path.join(source,'distribution/files.json'),path.join(destination,'distribution/files.json'));
 for(const name of ['data','dist'])await fs.mkdir(path.join(destination,name),{recursive:true});return files+1;
}
module.exports={inventory,verify,copySource};
