'use strict';
const test=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs/promises'),path=require('node:path'),os=require('node:os');
const ui=require('../panel/ui/presentation.js'),errors=require('../language/error-text.js'),root=path.resolve(__dirname,'..');
test('running and retained offline sessions keep the overview; first setup keeps the full form',()=>{
 assert.equal(ui.session({link:{state:'waiting-for-network'}}).offline,false);assert.equal(ui.session({active:{status:'running'}}).compact,true);assert.equal(ui.session({civLauncher:{state:'running'}}).compact,true);
 assert.equal(ui.session({active:{status:'offline'}}).compact,true);assert.equal(ui.session({civLauncher:{state:'offline'}}).offline,true);assert.equal(ui.session({active:{status:'typing',kind:'bootstrap'}}).compact,false);
 assert.equal(ui.session({active:{status:'running'},link:{state:'waiting-for-network'}}).offline,true);
 const stopping=ui.session({active:{kind:'launch',status:'stopping',nativeParentRetention:{awaitingActualClose:true}}});assert.equal(stopping.compact,true);assert.equal(stopping.title,'Shutdown unconfirmed');assert.equal(stopping.stopping,true);
});
test('completed removal cannot inherit installation or loading labels',()=>{
 assert.equal(ui.job({status:'ready',target:'sd',operation:'remove'}).label,'Removed from SD');assert.equal(ui.job({status:'complete',target:'session',operation:'remove'}).label,'Removed from session');
 assert.equal(ui.job({status:'ready',target:'sd'}).complete,true);assert.equal(ui.job({status:'loading'}).active,true);assert.equal(ui.job({status:'failed'}).failed,true);
});
test('the PC version never substitutes for an unknown observed SD version',()=>{
 const pkg={id:'app',version:'2.0.0',sha256:'new'};
 assert.equal(ui.versions({sdInstalled:[{id:'app',version:'1.0.0'}]},pkg).sd,null);
 const v=ui.versions({sdInstalled:[{id:'app',version:'1.0.0',versionVerified:true,sourceSha256:'old'}]},pkg);assert.equal(v.sd,'1.0.0');assert.equal(v.sdUpdate,true);
});
test('history filters and pagination preserve access to all attempts',()=>{
 const rows=Array.from({length:24},(_,i)=>({status:i%2?'failed':'cancelled',error:'message '+i}));assert.equal(ui.history(rows).rows.length,10);assert.equal(ui.history(rows).more,true);assert.equal(ui.history(rows,{kind:'failed',limit:30}).rows.length,12);assert.equal(ui.history(rows,{query:'message 23'}).total,1);
});
test('canonical diagnostics retain English stack traces and never splice Russian prose into other locales',()=>{
 const english='Error: socket hang up\n at launch (/home/user/app.cjs:12:3)';assert.equal(errors.canonical(english),english);
 const legacy='\u041e\u0448\u0438\u0431\u043a\u0430: \u041f\u043e\u0434\u043a\u043b\u044e\u0447\u0435\u043d\u0438\u0435 activation \u043e\u0448\u0438\u0431\u043a\u0430: \u0443\u0441\u0442\u0440\u043e\u0439\u0441\u0442\u0432\u043e \u043d\u0435\u0434\u043e\u0441\u0442\u0443\u043f\u043d\u043e';assert(!/[\u0410-\u042f\u0430-\u044f\u0401\u0451]/.test(errors.canonical(legacy)));
 assert.match(errors.canonical('\u041e\u0448\u0438\u0431\u043a\u0430: /home/user/\u0418\u0433\u0440\u044b/game.bin'),/\/home\/user\/\u0418\u0433\u0440\u044b\/game.bin/);
 assert.match(errors.describe('Native execution uncertain; resources retained').action,/Restart the game/);
 assert(!/try again/i.test(errors.describe('Native execution uncertain; resources retained').action));
});
test('SD package discovery can update while executable session providers remain pinned',async()=>{
 const directory=await fs.mkdtemp(path.join(os.tmpdir(),'iz-ui-pins-')),home=path.join(directory,'modules/izpsx'),old=process.env.IZ2S_MODULE_PINS;
 try{
  await fs.mkdir(home,{recursive:true});const archive=require('../core/archive.cjs'),currentBytes=await fs.readFile(path.join(root,'modules/izpsx/izpsx.izm'));const entries=archive.inspect(currentBytes).files.filter(f=>f.name!=='checksums.json').map(f=>{if(['module.json','switch/app.json'].includes(f.name)){const d=JSON.parse(f.bytes);d.version='0.0.1-snapshot';return {name:f.name,bytes:Buffer.from(JSON.stringify(d))};}return f;});entries.push({name:'checksums.json',bytes:Buffer.from(JSON.stringify({algorithm:'sha256',files:Object.fromEntries(entries.map(f=>[f.name,archive.sha(f.bytes)]))}))});await fs.writeFile(path.join(home,'izpsx.izm'),archive.encode(entries));
  const modules=require('../core/modules.cjs'),capture=await modules.capture(directory);process.env.IZ2S_MODULE_PINS=JSON.stringify(capture);
  const before=await modules.read(directory,'izpsx');await fs.copyFile(path.join(root,'modules/izpsx/izpsx.izm'),path.join(home,'izpsx.izm'));
  const current=(await modules.list(directory,{pinned:false})).modules[0],pinned=await modules.read(directory,'izpsx');assert.equal(pinned.descriptor.version,before.descriptor.version);assert.notEqual(current.sha256,pinned.sha256);
  const repo=await require('../core/packages/repository.cjs').create(directory,{source:'current'}),pkg=await repo.inspect('izpsx.izm');assert.equal(pkg.sha256,current.sha256);const preview=await repo.previewApps(path.join(directory,'preview'));assert.equal(preview.apps.length,1);assert.equal(preview.packages[0].sha256,current.sha256);const guarded=await require('../core/packages/repository.cjs').create(directory,{source:'current',sessionProviders:true});assert.equal((await guarded.inspect('izpsx.izm')).session.requiresFreshSession,true);await assert.rejects(guarded.install('izpsx.izm',path.join(directory,'blocked')),/new launcher session/);await repo.install('izpsx.izm',path.join(directory,'installed'),{expectedSha256:pkg.sha256});assert.equal((await modules.read(directory,'izpsx')).sha256,pinned.sha256);
 }finally{if(old===undefined)delete process.env.IZ2S_MODULE_PINS;else process.env.IZ2S_MODULE_PINS=old;await fs.rm(directory,{recursive:true,force:true});}
});

test('the loaded session command surface follows Refresh status and precedes device settings',async()=>{
 const html=await fs.readFile(path.join(root,'panel/ui/panel.html'),'utf8');
 assert(html.indexOf('id="session-summary"')>html.indexOf('id="device-toolbar"'));assert(html.indexOf('id="session-summary"')<html.indexOf('id="hardware-options-home"'));
});
test('Connection inventory shows observed RAM and SD apps, merges badges and excludes PC-only choices',()=>{
 const state={connected:true,platform:{},installed:['doom'],sdInstalled:[{id:'doom'},{id:'psx',name:'PlayStation'}],library:[{id:'doom',name:'DOOM'},{id:'pc-only',name:'Not loaded'}],startupModules:['pc-only']};
 assert.deepEqual(ui.consoleApps(state).map(a=>[a.id,a.ram,a.sd]),[['doom',true,true],['psx',false,true]]);
 assert.deepEqual(ui.consoleApps({...state,connected:false}),[]);
 assert.deepEqual(ui.consoleApps({...state,platform:{loaderId:'civswitch-ldn'}}).map(a=>[a.id,a.ram,a.sd]),[['doom',true,false]]);
 assert.deepEqual(ui.consoleApps({...state,platform:{sdLauncher:true}}).map(a=>[a.id,a.ram,a.sd]),[['doom',false,true],['psx',false,true]]);
});
test('library filters combine observed destination and status, including separate RAM and SD copies',()=>{
 const state={installed:['ram','both'],sdInstalled:[{id:'sd'},{id:'both'}],library:['ram','sd','both','absent'].map(id=>({id,name:'App '+id,description:'Description'}))};
 const filter=options=>ui.libraryFilter(state,options).ids;
 assert.deepEqual(filter({status:'installed'}),['ram','sd','both']);assert.deepEqual(filter({status:'uninstalled'}),['absent']);
 assert.deepEqual(filter({status:'installed',location:'ram'}),['ram','both']);assert.deepEqual(filter({status:'installed',location:'sd'}),['sd','both']);
 assert.deepEqual(filter({status:'uninstalled',location:'sd'}),['ram','absent']);assert.deepEqual(filter({status:'uninstalled',location:'ram'}),['sd','absent']);
 assert.deepEqual(filter({status:'uninstalled',location:'sd',query:'APP RAM'}),['ram']);
 assert.deepEqual(ui.libraryFilter({...state,platform:{sdLauncher:true}},{status:'installed',location:'ram'}).ids,[]);
 assert.deepEqual(ui.libraryFilter(undefined).ids,[]);
});
test('Connection deep links filter by the exact module ID when several versions have the same name',()=>{
 const state={library:[{id:'ps2-a',name:'IZPS2'},{id:'ps2-b',name:'IZPS2'}]};
 assert.deepEqual(ui.libraryFilter(state,{focusId:'ps2-b'}).ids,['ps2-b']);
 assert.deepEqual(ui.libraryFilter(state,{focusId:'removed'}).ids,[]);
});

test('SmileBASIC input hardware yields to the network after connection and throughout recovery',()=>{
 const base={platform:{loaderId:'izmysh'}};
 for(const extra of [{active:{kind:'launch',status:'starting',launchStage:'initializing'},link:{state:'synchronizing'}},{active:{kind:'launch',status:'starting',launchStage:'initializing'},link:{state:'connected'}},{connected:true},{active:{kind:'launch',status:'running'}},{active:{kind:'launch',status:'offline'}},{active:{kind:'launch',status:'running'},link:{state:'waiting-for-network'}},{active:{kind:'launch',status:'offline'},link:{protocol:3,recoveryAvailable:true}},{active:{kind:'launch',status:'stopping'}}])assert.equal(ui.directNetwork({...base,...extra}),true);
 for(const extra of [{},{active:{kind:'launch',status:'starting'}},{active:{kind:'bootstrap',status:'typing'}},{active:{kind:'bootstrap',status:'running'},connected:true}])assert.equal(ui.directNetwork({...base,...extra}),false);
 for(const loaderId of ['civswitch','civswitch-ldn'])assert.equal(ui.directNetwork({platform:{loaderId},connected:true,active:{kind:'launch',status:'running'}}),false);
});

test('FS acquisition failures identify the failed stage while preserving native retention diagnostics',()=>{
 const message='FS RPC memory acquisition failed: native execution did not finish reliably. Resources retained; automatic retry disabled';
 const result=errors.describe(message);
 assert.equal(result.title,'Filesystem access was not acquired in this attempt.');
 assert.match(result.action,/Fully close and reopen the game/);assert.equal(result.detail,message);
 assert.equal(errors.describe('Native execution uncertain; resources retained').title,'Native execution could not be confirmed.');
});
