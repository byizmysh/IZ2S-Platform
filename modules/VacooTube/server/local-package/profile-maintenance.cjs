// Local-only inspection and quality preferences; no YouTube network requests.
const path=require('node:path');
async function main({app,BrowserWindow,session}){
 const profile=process.env.IZ_YOUTUBE_PROFILE_DIRECTORY||path.join(__dirname,'data');
 app.setName('VacooTube');app.setPath('userData',profile);app.setPath('sessionData',path.join(profile,'sessionData'));
 if(!app.requestSingleInstanceLock())throw Error('Close YouTube before opening its local profile for maintenance');
 const timeout=setTimeout(()=>app.exit(1),10000);timeout.unref();
 await app.whenReady();const ses=session.defaultSession,target='https://www.youtube.com/iz-profile-maintenance';
 await ses.protocol.handle('https',request=>{if(request.url!==target)throw Error('Network access is disabled during profile maintenance');return new Response('<!doctype html><title>Local profile maintenance</title>',{headers:{'Content-Type':'text/html'}});});
 const w=new BrowserWindow({show:false,webPreferences:{offscreen:true,backgroundThrottling:false}});w.webContents.setAudioMuted(true);await w.loadURL(target);
 if(process.env.IZ_YOUTUBE_PROFILE_QUALITY==='480')await w.webContents.executeJavaScript(require('./source-video.cjs').pageScript);
 const result=await w.webContents.executeJavaScript(`(async()=>({quality:JSON.parse(localStorage.getItem('yt-player-quality')||'null'),localWatchHistoryKeys:Object.keys(localStorage).filter(k=>/watch.?history|view.?history|recently.?watched/i.test(k)),indexedDatabaseCount:(await indexedDB.databases()).length}))()`);
 console.log('IZ_PROFILE '+JSON.stringify(result));ses.flushStorageData();w.destroy();clearTimeout(timeout);app.quit();
}
module.exports={main};
