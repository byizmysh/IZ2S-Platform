// Loading the console module never installs or embeds the desktop runtime.
async function create(directory,options={}){
 const server=await require(require('node:path').join((require('./platform.cjs').root),"core/modules.cjs")).loadServer(require('./platform.cjs').root,'vacootube');
 const local=server.load('localPackage'),app=await local.create(directory,options),input=require('./youtube-input.cjs').create(app.send,{size:()=>app.report.profile});
 return {...app,input:input.input,release:input.release,async close(){input.release();await app.close();}};
}
module.exports={create};
