// Runs inside the pinned upstream Electron runtime. No listening debugger port.
const electron=require('electron'),fs=require('node:fs'),path=require('node:path'),readline=require('node:readline');
const {app}=electron,profileDirectory=process.env.IZ_YOUTUBE_SELF_TEST==='1'?path.join(__dirname,'test-data'):(process.env.IZ_YOUTUBE_PROFILE_DIRECTORY||path.join(require('./module-home.cjs'),'data/profile'));
let {width:WIDTH,height:HEIGHT}=require('./render-sizes.cjs').profile(process.env.IZ_VACOOTUBE_RENDER_HEIGHT||720);
app.setPath('userData',profileDirectory);
app.commandLine.appendSwitch('force-device-scale-factor','1');
app.commandLine.appendSwitch('lang','en-US');
let win,capture,closed=false,touchActive=false,inputRevision=0,committedInputRevision=0,inputPreparing;
const control=fs.createReadStream(null,{fd:3,autoClose:false}),frames=fs.createWriteStream(null,{fd:4,autoClose:false});
const send=data=>process.stdout.write('IZYOUTUBE '+JSON.stringify(data)+'\n');
const quit=()=>{if(closed)return;closed=true;capture?.close();app.quit();};
control.on('end',quit);control.on('error',quit);frames.on('error',quit);
async function ensureInput(wc){
 if(!wc.debugger.isAttached()){wc.debugger.attach('1.3');inputPreparing=null;}
 inputPreparing??=wc.debugger.sendCommand('Emulation.setTouchEmulationEnabled',{enabled:true,maxTouchPoints:1});
 try{await inputPreparing;}catch(error){inputPreparing=null;throw error;}
}
app.on('browser-window-created',(_event,w)=>{
 if(win)return;win=w;w.setContentSize(WIDTH,HEIGHT);w.setResizable(false);w.webContents.setBackgroundThrottling(false);
 if(process.env.IZ_YOUTUBE_MUTED==='1')w.webContents.setAudioMuted(true);
 // Upstream's ready-to-show and second-instance handlers cannot expose this window.
 for(const name of ['show','showInactive','focus','setFullScreen','setAlwaysOnTop'])w[name]=()=>{};
 w.webContents.setFrameRate(240);w.webContents.setWindowOpenHandler(()=>({action:'deny'}));
 capture=require('./frame-capture.cjs').create({invalidate:()=>w.webContents.invalidate(),beforeCapture:async()=>{
  await inputDispatch.idle();
  if(inputRevision!==committedInputRevision){
   const own=inputRevision;
   // CDP acknowledges dispatch before Chromium commits the resulting style
   // update. Cross that renderer frame boundary before invalidating offscreen
   // output; otherwise the first forced paint can still contain the old UI.
   await w.webContents.executeJavaScript('new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve)))');
   committedInputRevision=own;
   // Electron can already have an older GPU readback queued for delivery even
   // after the renderer frame boundary. Drain that first paint before sending
   // the next newly invalidated image to the host.
   return {discardPaints:1};
  }
 },onWait:m=>send({type:'frame-wait',...m}),onError:e=>send({type:'capture-error',error:e.message}),write:async frame=>{
  let image=frame.image;if(!image||image.isEmpty())throw Error('Empty YouTube compositor paint');
  const size=image.getSize();if(size.width!==WIDTH||size.height!==HEIGHT)image=image.resize({width:WIDTH,height:HEIGHT,quality:'good'});
  // Older live hosts request without an ID. Retain their original wire format
  // so a separately loaded local module does not break an older host process.
  const versioned=frame.requestId!==0,bitmap=image.toBitmap(),header=Buffer.alloc(versioned?32:16);header.write(versioned?'IYF2':'IYF1');header.writeUInt32LE(bitmap.length,4);header.writeUInt32LE(WIDTH,8);header.writeUInt32LE(HEIGHT,12);
  if(versioned){header.writeUInt32LE(frame.requestId>>>0,16);header.writeUInt32LE(frame.revision>>>0,20);header.writeDoubleLE(frame.at,24);}
  await new Promise((resolve,reject)=>{frames.cork();frames.write(header);frames.write(bitmap,error=>error?reject(error):resolve());frames.uncork();});
 }});
 // Chromium can publish an empty startup readback before its first real paint.
 // Keep the request pending; an empty image must never satisfy frame freshness.
 w.webContents.on('paint',(_event,_dirty,image)=>{if(image&&!image.isEmpty())capture.paint(image);});
 if(process.env.IZ_YOUTUBE_SELF_TEST==='1')w.webContents.on('console-message',event=>{const message=event.message;if(message?.startsWith('IZSELFTEST '))send({type:'fixture',state:JSON.parse(message.slice(11))});});
 w.webContents.on('did-finish-load',async()=>{touchActive=false;
  if(process.env.IZ_YOUTUBE_SELF_TEST!=='1'&&new URL(w.webContents.getURL()).hostname==='www.youtube.com'){try{await w.webContents.executeJavaScript(require('./source-video.cjs').pageScript);}catch(error){send({type:'error',error:'Video quality: '+error.message});}}
  // Attach the private input channel during loading rather than charging its
  // one-time setup cost to the user's first touch.
  try{await ensureInput(w.webContents);}catch(error){send({type:'error',error:error.message});}
  send({type:'ready',pid:process.pid,width:WIDTH,height:HEIGHT,hidden:!w.isVisible(),offscreen:w.webContents.isOffscreen(),url:w.webContents.getURL()});
 });
 w.on('closed',()=>{win=null;quit();});
});
async function command(m){
 if(m.type==='close')return quit();if(!win||win.isDestroyed())return;
 const wc=win.webContents;
 if(m.type==='resize'){const next=require('./render-sizes.cjs').profile(m.height);if(next.width!==m.width||!Number.isInteger(m.resizeId))throw Error('Invalid VacooTube render size');capture.reset();WIDTH=next.width;HEIGHT=next.height;touchActive=false;win.setContentSize(WIDTH,HEIGHT);send({type:'resized',resizeId:m.resizeId,width:WIDTH,height:HEIGHT});return;}
 if(m.type==='frame'){
  await capture.request(m.requestId>>>0);return;
 }
 if(m.type==='touch'){
  if(!['touchStart','touchMove','touchEnd','touchCancel'].includes(m.phase))throw Error('Invalid touch phase');
  if((m.phase==='touchEnd'||m.phase==='touchCancel')&&!touchActive)return;
  await ensureInput(wc);
  const points=m.phase==='touchEnd'||m.phase==='touchCancel'?[]:[{x:Math.max(0,Math.min(WIDTH-1,Number(m.x)||0)),y:Math.max(0,Math.min(HEIGHT-1,Number(m.y)||0)),id:0,radiusX:6,radiusY:6,force:1}];
  await wc.debugger.sendCommand('Input.dispatchTouchEvent',{type:m.phase,touchPoints:points});touchActive=m.phase==='touchStart'||m.phase==='touchMove';return;
 }
 if(m.type==='key'&&['Up','Down','Left','Right','Return','Escape','Space','Backspace','Tab'].includes(m.key)&&['keyDown','keyUp'].includes(m.phase)){
  await ensureInput(wc);const names={Up:['ArrowUp',38],Down:['ArrowDown',40],Left:['ArrowLeft',37],Right:['ArrowRight',39],Return:['Enter',13],Escape:['Escape',27],Space:[' ',32],Backspace:['Backspace',8],Tab:['Tab',9]},[key,code]=names[m.key];await wc.debugger.sendCommand('Input.dispatchKeyEvent',{type:m.phase,key,code:m.key==='Space'?'Space':key,windowsVirtualKeyCode:code,nativeVirtualKeyCode:code,...(m.phase==='keyDown'&&m.key==='Return'?{text:'\r'}:{})});return;
 }
 if(m.type==='text'&&typeof m.text==='string'&&m.text.length<=256){await ensureInput(wc);await wc.debugger.sendCommand('Input.insertText',{text:m.text});return;}
}
const inputDispatch=require('./input-dispatch.cjs').create(async m=>{await command(m);inputRevision++;capture?.inputChanged();},e=>send({type:'error',error:e.message}));
readline.createInterface({input:control,crlfDelay:Infinity}).on('line',line=>{if(line.length>4096)return quit();let m;try{m=JSON.parse(line);}catch{return;}if(m.type==='close')quit();else if(m.type==='frame')command(m).catch(e=>send({type:'error',error:e.message}));else inputDispatch.push(m);});
const runtime={app:process.env.IZ_YOUTUBE_APP_SOURCE||path.join(require('./module-home.cjs'),'runtime','VacooTube-'+process.arch,'resources/app-source')};
process.argv=[process.argv[0],`--portable=${profileDirectory}`,'--width='+WIDTH,'--height='+HEIGHT,'--no-window-decorations'];
// Keep the fork source immutable, with a dedicated profile and streaming preferences.
fs.mkdirSync(profileDirectory,{recursive:true});const configFile=path.join(profileDirectory,'config.json');let config={};try{config=JSON.parse(fs.readFileSync(configFile,'utf8'));}catch{}
fs.writeFileSync(configFile,JSON.stringify({...config,pause_on_blur:false,fullscreen:false,no_window_decorations:true,touch_overlay:false,hardware_decoding:true,auto_update:false,device_discoverability:false}),{mode:0o600});
// One narrow source adaptation sets construction-time Electron rendering options.
// The verified fork source keeps its relative imports; construction options are scoped.
if(process.env.IZ_YOUTUBE_SELF_TEST==='1'){
 app.whenReady().then(()=>{const fixture=new electron.BrowserWindow({width:WIDTH,height:HEIGHT,show:false,webPreferences:{offscreen:true,backgroundThrottling:false}});fixture.loadFile(path.join(__dirname,'test-fixture.html'));});
}else{
const Module=require('node:module'),entry=path.join(runtime.app,'src/index.js');let source=fs.readFileSync(entry,'utf8');
for(const [needle,replacement]of [['win = new electron.BrowserWindow({','win = new electron.BrowserWindow({\n        show: false, skipTaskbar: true,'],['webPreferences: {','webPreferences: {\n            offscreen: true, backgroundThrottling: false,']]){if(source.split(needle).length!==2)throw Error('Unsupported VacooTube window construction');source=source.replace(needle,replacement);}
const upstream=new Module(entry,module);upstream.filename=entry;upstream.paths=Module._nodeModulePaths(path.dirname(entry));upstream._compile(source,entry);
}
