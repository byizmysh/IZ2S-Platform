'use strict';
const http=require('node:http'),fs=require('node:fs/promises'),path=require('node:path'),crypto=require('node:crypto');
async function open({moduleHome,services}){
 const store=await require('./store.cjs').open(moduleHome),token=crypto.randomBytes(24).toString('hex');let origin;
 const server=http.createServer(async(req,res)=>{
  res.setHeader('cache-control','no-store');res.setHeader('x-content-type-options','nosniff');res.setHeader('referrer-policy','no-referrer');res.setHeader('content-security-policy',"default-src 'self'; script-src 'self'; style-src 'self'; connect-src 'self'; img-src 'self' data:; frame-ancestors 'none'; base-uri 'none'");
  const reply=(code,data)=>{res.writeHead(code,{'content-type':'application/json'});res.end(JSON.stringify(data));};
  try{
   if(req.headers.host!==new URL(origin).host)throw Error('Use the local reader settings link');const url=new URL(req.url,origin),asset={'/':['settings.html','text/html; charset=utf-8'],'/reader.js':['settings.js','text/javascript'],'/reader.css':['settings.css','text/css']};
   if (await require(path.join(require('./platform.cjs').root,'language/server.cjs')).serve(req, res)) return;
   if(req.method==='GET'&&asset[url.pathname]){const [file,type]=asset[url.pathname];res.writeHead(200,{'content-type':type});res.end(await fs.readFile(path.join(__dirname,file)));return;}
   if(req.headers['x-reader-key']!==token)return reply(403,{error:'Reopen Reader settings from IZ2S Platform'});
   if(req.method==='GET'&&url.pathname==='/state')return reply(200,{preferences:await store.preferences(),books:await store.library(),choices:require('./layout.cjs').choices,inbox:store.inbox});
   if(req.method!=='POST'||req.headers.origin!==origin)return reply(403,{error:'Use the reader settings page'});
   const parts=[];let size=0;for await(const bytes of req){size+=bytes.length;if(size>16*1024*1024)throw Error('Upload exceeds 16 MiB');parts.push(bytes);}const body=Buffer.concat(parts);
   if(url.pathname==='/upload')return reply(200,{book:await store.upload(url.searchParams.get('name')||'',body)});
   if(url.pathname==='/scan')return reply(200,await store.scan());
   if(url.pathname==='/preferences'){if(body.length>4096)throw Error('Settings are too large');const patch=JSON.parse(body),preferences={...await store.preferences(),...patch};if(preferences.bookId){const book=await store.readBook(preferences.bookId),metrics=JSON.parse(await fs.readFile(path.join(__dirname,'../switch/assets/font.json'),'utf8'));require('./layout.cjs').view({text:book.text,preferences,metrics,books:await store.library()});}return reply(200,{preferences:await store.setPreferences(patch)});}
   reply(404,{error:'Unknown reader action'});
  }catch(error){reply(400,{error:error.message});}
 });
 const bound=await services.listenHttp(server,{port:0});origin=bound.origin;
 return {url:origin+'/#'+token,async close(){server.closeAllConnections();await new Promise(r=>server.close(r));}};
}
module.exports={open};
