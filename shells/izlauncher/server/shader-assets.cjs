"use strict";
// Reuse a matching prepared shell; runtime installation never compiles source.
const fs=require('node:fs/promises'),path=require('node:path');
async function stage(folder){
 const source=path.join(__dirname,'../app'),original=path.join(source,'shaders'),requested=path.join(folder,'shaders');
 const names=(await fs.readdir(original)).sort(),actual=(await fs.readdir(requested)).sort();
 const unavailable=()=>Error('Source-based shell contributions require preparation in the development SDK; install a precompiled application.');
 if(JSON.stringify(names)!==JSON.stringify(actual))throw unavailable();
 for(const name of names)if(!(await fs.readFile(path.join(original,name))).equals(await fs.readFile(path.join(requested,name))))throw unavailable();
 const manifest=JSON.parse(await fs.readFile(path.join(folder,'app.json'))),ready=JSON.parse(await fs.readFile(path.join(source,'app.json')));
 for(const name of new Set(Object.values(ready.shaders)))await fs.copyFile(path.join(source,name),path.join(folder,name));
 manifest.shaders=ready.shaders;manifest.shaderEncoding=ready.shaderEncoding;
 await fs.writeFile(path.join(folder,'app.json'),JSON.stringify(manifest));
}
module.exports={stage};
