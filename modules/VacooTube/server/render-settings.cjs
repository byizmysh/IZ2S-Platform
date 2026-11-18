const fs=require('node:fs/promises'),path=require('node:path'),sizes=require('./local-package/render-sizes.cjs'),{selected}=require('./streaming/stream.cjs');
async function open({file=path.join(require('./platform.cjs').home,'data/render-settings.json')}={}){
 let height=720,engine='run13';try{const saved=JSON.parse(await fs.readFile(file,'utf8'));height=sizes.profile(saved.height).height;engine=selected(saved.engine);}catch(e){if(e.code!=='ENOENT')throw e;}
 let work=Promise.resolve();
 function update(patch){const task=work.catch(()=>{}).then(async()=>{const next={height,...{engine},...patch};await fs.mkdir(path.dirname(file),{recursive:true});const temp=file+'.tmp';await fs.writeFile(temp,JSON.stringify(next)+'\n',{mode:0o600});await fs.rename(temp,file);height=next.height;engine=next.engine;return sizes.profile(height);});work=task;return task;}
 return {profile:()=>sizes.profile(height),engine:()=>engine,set:value=>update({height:sizes.profile(value).height}),setEngine:value=>update({engine:selected(value)})};
}
module.exports={open,...sizes};
