// Optional host desktop integration; the panel itself runs without a GUI opener.
const fs=require('node:fs'),path=require('node:path'),{execFileSync}=require('node:child_process');
function browserCommand(url,{env=process.env,platform=process.platform,exists=fs.existsSync,query=(program,args)=>execFileSync(program,args,{encoding:'utf8',timeout:1500,stdio:['ignore','pipe','ignore']})}={}) {
  const searchPath=env[Object.keys(env).find(key=>key.toUpperCase()==='PATH')]||'';
  const locate=name=>{if(name.includes('/')||name.includes('\\'))return exists(name)?name:null;const separator=platform==='win32'?';':path.delimiter;for(const dir of searchPath.split(separator).filter(Boolean)){const file=(platform==='win32'?path.win32:path).join(dir,name);if(exists(file))return file;}return null;};
  const flags=program=>/firefox/i.test(program)?['--new-window',url]:['--new-window',url];
  const explicit=env.IZ2S_BROWSER||env.HB_CHROME;
  if(explicit){const program=locate(explicit);if(!program)throw Error('The selected browser was not found');return [program,flags(program)];}
  let preferred='';
  try {
    if(platform==='linux')preferred=query('xdg-settings',['get','default-web-browser']).trim();
    else if(platform==='win32')preferred=query('reg.exe',['query','HKCU\\Software\\Microsoft\\Windows\\Shell\\Associations\\UrlAssociations\\http\\UserChoice','/v','ProgId']);
    else if(platform==='darwin'){
      const handlers=query('/usr/bin/defaults',['read','com.apple.LaunchServices/com.apple.launchservices.secure','LSHandlers']);
      const blocks=[...handlers.matchAll(/\{([^{}]*)\}/g)].map(match=>match[1]);
      const web=blocks.find(block=>/LSHandlerURLScheme\s*=\s*"?https"?\s*;/i.test(block))||blocks.find(block=>/LSHandlerURLScheme\s*=\s*"?http"?\s*;/i.test(block));
      preferred=web?.match(/LSHandlerRoleAll\s*=\s*"?([^";\s]+)"?\s*;/i)?.[1]||'';
    }
  }catch{}
  if(platform==='darwin'){
    const app=/firefox/i.test(preferred)?'Firefox':/google.chrome/i.test(preferred)?'Google Chrome':/microsoft.edg/i.test(preferred)?'Microsoft Edge':/brave/i.test(preferred)?'Brave Browser':/vivaldi/i.test(preferred)?'Vivaldi':/opera/i.test(preferred)?'Opera':/apple.safari/i.test(preferred)||!preferred?'Safari':null;
    if(!app)return ['/usr/bin/open',[url]];
    return ['/usr/bin/open',app==='Safari'?['-n','-a',app,url]:['-a',app,'--args','--new-window',url]];
  }
  const families=[[/firefox/i,['firefox','firefox.exe']],[/chromium/i,['chromium','chromium-browser']],[/brave/i,['brave-browser','brave.exe']],[/vivaldi/i,['vivaldi-stable','vivaldi']],[/edge|microsoft/i,['microsoft-edge','msedge.exe']],[/chrome/i,['google-chrome-stable','google-chrome','chrome.exe']]];
  const family=families.find(([match])=>match.test(preferred));
  if(family)for(const name of family[1]){const program=locate(name);if(program)return [program,flags(program)];}
  if(platform==='win32'){
    const base=env.ProgramFiles||'C:\\Program Files',local=env.LOCALAPPDATA||'';
    const candidates=/firefox/i.test(preferred)?[base+'\\Mozilla Firefox\\firefox.exe']:/edge/i.test(preferred)?[(env['ProgramFiles(x86)']||base)+'\\Microsoft\\Edge\\Application\\msedge.exe']:/chrome/i.test(preferred)?[base+'\\Google\\Chrome\\Application\\chrome.exe',local+'\\Google\\Chrome\\Application\\chrome.exe']:[];
    for(const program of candidates)if(exists(program))return [program,flags(program)];
    return ['rundll32.exe',['url.dll,FileProtocolHandler',url]];
  }
  for(const [,names] of families)for(const name of names){const program=locate(name);if(program)return [program,flags(program)];}
  return ['xdg-open',[url]];
}

function folderCommand(folder,{platform=process.platform,env=process.env,exists=fs.existsSync}={}){
 if(platform==='darwin')return ['/usr/bin/open',[folder]];
 if(platform==='win32')return ['explorer.exe',[folder]];
 for(const program of ['xdg-open','gio','dolphin','nautilus','nemo','thunar','pcmanfm','caja'])
  for(const directory of (env.PATH||'').split(path.delimiter)){
   const file=path.join(directory,program);
   if(exists(file))return [file,program==='gio'?['open',folder]:[folder]];
  }
 throw Error('No desktop file manager was found. Open this folder manually: '+folder);
}
module.exports={browserCommand,folderCommand};
