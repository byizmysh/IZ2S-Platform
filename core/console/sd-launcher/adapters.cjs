'use strict';
// The launcher supplies local controllers for exact existing builds or an
// explicit instruction plan. Existing native app entries stay authoritative.
function eligibility(manifest,files){
 const instructions=require('./instructions.cjs').eligibility(manifest,files);if(instructions)return instructions;
 const reader=require('./reader/prepare.cjs').eligibility(manifest,files);if(reader)return reader;
 // Existing native adapters take precedence. Card storage of a PC executor
 // must never advertise autonomous execution through the compatibility flag.
 if(manifest.hostRuntime&&!manifest.native&&!manifest.localRuntime&&!manifest.builtin&&!manifest.displayOnly&&!manifest.shellContribution)
  return {compatible:false,storageCompatible:true,adapter:'pc-managed',execution:'pc',hostRequired:true,reason:'Requires the PC server when launched.'};
 const policy=require('./package.cjs').eligibility(manifest);if(!policy.compatible)return policy;
 try{require('./assets.cjs').inspectPacked(manifest,files);}catch(e){return {compatible:false,reason:e.message};}return policy;
}
async function prepare(app,directory){return await require('./instructions.cjs').prepare(app)||await require('./reader/prepare.cjs').prepare(app,directory)||app;}
module.exports={eligibility,prepare};
