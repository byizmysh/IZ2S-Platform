'use strict';
const fs=require('node:fs/promises'),path=require('node:path'),assert=require('node:assert/strict');
const root=path.resolve(__dirname,'..');
async function main(){
 const checked=await require('./runtime-files.cjs').verify(root);
 const p=require('./profile.json'),D=require('../core/descriptors.cjs'),A=require('../core/archive.cjs');
 const trusted=new Set(require('../core/services/bundled-shaders.json').sha256);
 for(const [name,file]of Object.entries(checked.manifest.files))if(name.endsWith('.spv'))assert(trusted.has(file.sha256),'Bundled shader is absent from the validated runtime catalog: '+name);
 for(const kind of ['engine','shell','profile']){const s=D.scan(root,kind);assert.deepEqual(s.errors,[]);assert(s.items.length);}
 for(const s of p.shells)require('../core/session/runtime-check.cjs').verify(root,s);
 require('../core/console/sd-launcher/check.cjs').verifyRuntime();
 const core=await require('../engines/iz2s-core/runtime.cjs').verify();
 const ids=[];
 for(const folder of p.moduleDirectories){const home=path.join(root,'modules',folder),d=JSON.parse(await fs.readFile(path.join(home,'module.json'))),b=await fs.readFile(path.join(home,d.id+'.module.zip'));assert.equal(A.inspect(b).descriptor.id,d.id);assert(A.sameContents(b,await A.pack(home)),'Stale package: '+d.id);ids.push(d.id);}
 assert.deepEqual(ids,p.modules);assert.equal(ids.length,6);
 const h=require('../hardware/agents.cjs');await h.files(root);await h.installer(root,'sb4');await h.installer(root,'civ7');
 for(const folder of ['engines/CivSwitch/installer-creator','engines/CivSwitchLDN']){const f=require.resolve('terser',{paths:[path.join(root,folder)]});assert(f.startsWith(path.join(root,folder,'node_modules')));assert.equal(typeof require(f).minify,'function');}
 console.log(JSON.stringify({verified:true,edition:'0.25',platform:p.releaseTarget,files:checked.files,bytes:checked.bytes,modules:ids,coreFiles:core.files,scope:'release content and install resources; not native rebuild or hardware qualification'}));
}
main().catch(e=>{console.error(e.stack);process.exitCode=1;});
