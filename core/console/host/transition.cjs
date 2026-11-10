'use strict';
// The caller has joined the renderer. An app-only transition needs the SDK
// snapshot after the app initializes storage, rather than before and after.
// Outstanding SDK requests and controller changes still run for the old owner.
async function begin(sdk,{app,controls,appRequest=false,serviceRequest=false,connectionChanged=false}={}){
 const sampled=!appRequest||serviceRequest||connectionChanged;
 if(sampled)await sdk.atBoundary(app,controls);
 let finished=false;
 return {async finish(next,nextControls){
  if(finished)throw Error('SDK transition already finished');finished=true;
  // A failed launch/refresh can keep the same app identity while clearing its
  // storage. It still needs the SDK snapshot that was deferred above.
  if(!sampled||next!==app||nextControls!==controls)await sdk.atBoundary(next,nextControls);
 }};
}
module.exports={begin};
