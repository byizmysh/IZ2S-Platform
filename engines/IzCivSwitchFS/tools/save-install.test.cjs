'use strict';
const test=require('node:test'),assert=require('node:assert/strict'),vm=require('node:vm'),fs=require('node:fs'),path=require('node:path');
const source=fs.readFileSync(path.join(__dirname,'../persist/installer.js'),'utf8');
const TARGET='save:/saves/Single/IZ2SCIV.Civ7Save',ORDINARY='save:/saves/Single/MyGame.Civ7Save';
function fixture({writeFail=false,commitFail=false,freeSpace=10000000}={}){
 const files=new Map([[TARGET,'previous IZ2S'],[ORDINARY,'ordinary save']]);let commits=0;
 const fake={exists:p=>files.has(p),mkdir:()=>{},freeSpace:()=>freeSpace,read:async p=>{if(!files.has(p))throw Error('Missing');return files.get(p);},
  writeNew:async(p,v)=>{files.set(p,v);if(writeFail)throw Error('Write failed');},delete:p=>files.delete(p),
  rename:(from,to)=>{assert(files.has(from));assert(!files.has(to));files.set(to,files.get(from));files.delete(from);},
  writeAt:async()=>({ok:true}),commit:()=>{commits++;return {ok:!commitFail,error:'Commit failed'};},dispose:()=>{}};
 const candidate=new Uint8Array([1,2,3]);
 const window={__cfsSaveCodec:{replace:()=>candidate,sha256Async:async()=> '32fa8a0d6dd0a480d8e541e63ea44c4619bc6f7be4282cfc3cb6a4f6d305dfae'}};
 vm.runInNewContext(source,{window,Uint8Array});
 const installer=window.__cfsCreateInstaller({exclusive:f=>f(),openFS:()=>fake});
 installer.offer({profile:'civ7-sdk-19f22ec5',version:'test',bytes:1,stage:'x'});
 const template=Buffer.alloc(1159730).toString('base64');
 for(let offset=0,i=0;offset<template.length;offset+=480000,i++)installer.stageChunk('fixture',i,offset,template.slice(offset,offset+480000));
 return {files,installer,candidate,get commits(){return commits;}};
}
test('installation replaces only IZ2SCIV without backup copies or changes to other saves',async()=>{
 const f=fixture();await f.installer.installFromTemplate();
 assert.equal(f.files.get(TARGET),f.candidate);assert.equal(f.files.get(ORDINARY),'ordinary save');
 assert.deepEqual([...f.files.keys()].sort(),[TARGET,ORDINARY].sort());assert.equal(f.commits,1);
 assert.equal([...f.files.keys()].filter(p=>p.endsWith('.Civ7Save')).length,2);
});
test('failed temporary write preserves the existing target and cleans up the incomplete file',async()=>{
 const f=fixture({writeFail:true});await assert.rejects(f.installer.installFromTemplate(),/failed/i);
 assert.equal(f.files.get(TARGET),'previous IZ2S');assert.equal(f.files.get(ORDINARY),'ordinary save');assert(!f.files.has('save:/iz2sciv-install.tmp'));assert.equal(f.installer.description().installed,false);
});
test('not enough free space leaves all existing saves untouched',async()=>{
 const f=fixture({freeSpace:0}),before=[...f.files];await assert.rejects(f.installer.installFromTemplate(),/Not enough save space/);assert.deepEqual([...f.files],before);assert.equal(f.commits,0);
});
test('failed commit never claims installation success or deletes the newly written target',async()=>{
 const f=fixture({commitFail:true});await assert.rejects(f.installer.installFromTemplate(),/Commit failed/);
 assert.equal(f.files.get(TARGET),f.candidate);assert.equal(f.files.get(ORDINARY),'ordinary save');
 assert.equal(f.installer.description().installed,false);
 assert.deepEqual([...f.files.keys()].sort(),[TARGET,ORDINARY].sort());
});

test('generated save renames only framed template game titles and preserves the pristine template',()=>{
 const bytes=fs.readFileSync(path.join(__dirname,'../persist/save_template.bin')), original=Buffer.from(bytes),window={};
 vm.runInNewContext(fs.readFileSync(path.join(__dirname,'../persist/save-codec.js'),'utf8'),{window,Uint8Array,DataView});
 const codec=window.__cfsSaveCodec,result=Buffer.from(codec.replace(new Uint8Array(bytes),'/* IZ2S fixture */',false,true));
 assert.deepEqual(bytes,original);assert(!result.includes(Buffer.from("Gezine's Game")));assert.equal(result.toString('latin1').split('IZ2S 0.3.2  ').length-1,2);
 assert.doesNotThrow(()=>codec.inspect(new Uint8Array(result)));
});

test('fresh installation writes the canonical save without requiring a previous file',async()=>{
 const f=fixture();f.files.delete(TARGET);
 await f.installer.installFromTemplate();
 assert.equal(f.files.get(TARGET),f.candidate);
 assert.equal(f.files.get(ORDINARY),'ordinary save');
 assert.deepEqual([...f.files.keys()].sort(),[TARGET,ORDINARY].sort());
});
test('built installer has exactly one game save target matching the source installer',async()=>{
 const {build}=await import('../node_modules/esbuild/lib/main.js');
 const result=await build({entryPoints:[path.join(__dirname,'../persist/installer.js')],bundle:true,write:false,format:'iife'});
 const bootstrap=result.outputFiles[0].text;
 const targets=[...new Set(bootstrap.match(/save:\/saves\/Single\/[^"']+/g))];
 assert.deepEqual(targets,[TARGET]);
});
