// Updates are managed as an IZ2S module, never fetched from another application.
let initialized=false;
const project='https://github.com/byizmysh/VacooTube',releases=project+'/releases';
function detectInstallation(){return {type:'iz2s-module',canAutoUpdate:false};}
function getManualDownloadUrl(){return releases;}
function setup({electron}){if(initialized)return;initialized=true;
 const state=()=>({status:'unsupported',currentVersion:require('../package.json').version,latestVersion:null,platform:process.platform,arch:process.arch,installationType:'iz2s-module',canAutoUpdate:false,progress:null});
 for(const channel of ['get-about-info','check-for-updates','download-update'])electron.ipcMain.handle(channel,async()=>state());
 electron.ipcMain.handle('install-update',async()=>false);
 for(const channel of ['open-project-page','open-release-page','open-update-download'])electron.ipcMain.handle(channel,async()=>{await electron.shell.openExternal(channel==='open-project-page'?project:releases);return true;});
 electron.ipcMain.on('can-auto-update',event=>{event.returnValue=false;});
}
module.exports={setup,onPreferenceChanged(){},detectInstallation,getManualDownloadUrl};
