'use strict';
// Keep the entry script self-contained when an older server is still running.
// storage.js remains the source for the shared destination controls.
const fs=require('node:fs/promises'),path=require('node:path');
async function build(root=path.resolve(__dirname,'..')){
 const ui=path.join(root,'panel/ui'),file=path.join(ui,'app.js');
 const source=(await fs.readFile(path.join(ui,'storage.js'),'utf8')).trimEnd();
 const begin='// BEGIN STORAGE CONTROLS',end='// END STORAGE CONTROLS';
 const block=begin+'\n'+source+'\n'+end+'\n';
 const original=await fs.readFile(file,'utf8'),first=original.indexOf(begin),last=original.indexOf(end);
 if((first<0)!==(last<0)||last>=0&&last<first)throw Error('Incomplete storage UI bundle');
 let next=first<0?original.replace(/^'use strict';\n/,"'use strict';\n"+block):original.slice(0,first)+block+original.slice(last+end.length).replace(/^\n/,'');
 const template=await fs.readFile(path.join(root,'sdk/arm-installer.py.in'),'utf8');
 const helper=(await fs.readFile(path.join(root,'sdk/arm-installer-ui.js.in'),'utf8')).replace("'__INSTALLER_TEMPLATE__'",JSON.stringify(template)).trimEnd();
 const armBegin='// BEGIN ARM INSTALLER',armEnd='// END ARM INSTALLER',armBlock=armBegin+'\n'+helper+'\n'+armEnd+'\n';
 const armFirst=next.indexOf(armBegin),armLast=next.indexOf(armEnd);
 if((armFirst<0)!==(armLast<0)||armLast>=0&&armLast<armFirst)throw Error('Incomplete ARM installer UI bundle');
 next=armFirst<0?next.replace(/^'use strict';\n/,"'use strict';\n"+armBlock):next.slice(0,armFirst)+armBlock+next.slice(armLast+armEnd.length).replace(/^\n/,'');
 for(const [name,sourceFile]of [['STARTUP','panel/ui/startup-choices.js'],['PRESENTATION','panel/ui/presentation.js'],['ERROR TEXT','language/error-text.js']]){
  const start='// BEGIN '+name,finish='// END '+name,body=(await fs.readFile(path.join(root,sourceFile),'utf8')).trimEnd(),block=start+'\n'+body+'\n'+finish+'\n';
  const a=next.indexOf(start),b=next.indexOf(finish);next=a<0?next.replace(/^'use strict';\n/,"'use strict';\n"+block):next.slice(0,a)+block+next.slice(b+finish.length).replace(/^\n/,'');
 }
 const policy=require(path.join(root,'core/packages/store-catalog.cjs')).createPolicy;
 const catalogStart=next.indexOf('const RUN21_CATALOG = '),catalogEnd=next.indexOf('})();',catalogStart);
 if(catalogStart<0||catalogEnd<0)throw Error('Missing catalog policy');
 next=next.slice(0,catalogStart)+'const RUN21_CATALOG = ('+policy.toString()+')();'+next.slice(catalogEnd+5);
 const runtimeFile=path.join(root,'language/runtime.js'),runtime=await fs.readFile(runtimeFile,'utf8'),errorSource=(await fs.readFile(path.join(root,'language/error-text.js'),'utf8')).trimEnd(),start='// BEGIN ERROR TEXT',finish='// END ERROR TEXT',a=runtime.indexOf(start),b=runtime.indexOf(finish),runtimeBlock=start+'\n'+errorSource+'\n'+finish+'\n';
 const nextRuntime=a<0?runtimeBlock+runtime:runtime.slice(0,a)+runtimeBlock+runtime.slice(b+finish.length).replace(/^\n/,'');if(nextRuntime!==runtime)await fs.writeFile(runtimeFile,nextRuntime);
 if(next===original)return false;
 await fs.writeFile(file,next);return true;
}
module.exports={build};
if(require.main===module)build().then(updated=>console.log(JSON.stringify({updated}))).catch(error=>{console.error(error.stack);process.exitCode=1;});
