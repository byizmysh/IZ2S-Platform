const path=require('node:path'),net=require('node:net');
const MANUAL_COMMANDS=Object.freeze(['LOAD "TXT:NS2"','RUN','REMOVE NSR,&H60000000,&H3FFFFFFF']);
function startup({root,manifest,arity=manifest.arity,runRoot,bootstrap,saveName='NS2',readyPatterns=[],completedPatterns=[],persistent=false}){
 const manualCommands=['LOAD "TXT:'+saveName+'"',...MANUAL_COMMANDS.slice(1)];
 return {bootstrap:{file:bootstrap||path.join(root,'upstream',manifest.bootstrap),saveName,arity,loaderId:manifest.id,revision:manifest.revision},manualCommands,manualSteps:launchPrefix(saveName),connect:{file:'connect.txt',stdoutPattern:'^\\s*(V=START\\([0-9,]+\\))\\s*$',appendOnly:true},runRoot,lifecycle:{runFolderPattern:'^Run folder: ([^\\r\\n]+)$',readyPatterns,completedPatterns,persistent,exitZeroCompletes:!persistent,restartConsoleAfterStop:true}};
}
function base(options){const host=options.host;if(!net.isIPv4(host||''))throw Error('Choose the IPv4 address reachable by the console');return {host,node:options.node||process.execPath};}
function inputSteps(plan,connectCommand){
 if(typeof connectCommand!=='string'||!/^V=START\([0-9,]+\)$/.test(connectCommand)||connectCommand.length>512)throw Error('Expected a fresh single-line START command from this loader');
 const words=connectCommand.slice(8,-1).split(',');if(![5,9].includes(words.length))throw Error('Unsupported upstream START arity');
 return [...plan.startup.manualSteps,{type:'line',text:connectCommand,delayMs:0}];
}
function connectSteps({command,bootstrapName='NS2',fromEditor=false,preloaded=false}={}){
 if(!/^[A-Z0-9_]{1,16}$/.test(bootstrapName)||typeof command!=='string'||!/^V=START\([0-9,]+\)$/.test(command)||command.length>512)throw Error('Expected saved bootstrap name and fresh START command');
 return [...(fromEditor?[{type:'key',key:'f4',delayMs:2000}]:[]),...launchPrefix(bootstrapName).slice(preloaded?2:0),{type:'line',text:command,delayMs:0}];
}
function launchPrefix(name){return [{type:'line',text:'LOAD "TXT:'+name+'"',delayMs:2000},{type:'key',key:'enter',delayMs:2000},{type:'line',text:'RUN',delayMs:3000},{type:'line',text:'REMOVE NSR,&H60000000,&H3FFFFFFF',delayMs:3000}];}
function izPlan({id,manifest,settings={},options={},bootstrap}){
 const {host,node}=base(options),root=path.resolve(__dirname,'..'),port=Number(options.port??options.consolePort??39380);
 if(!Number.isInteger(port)||port<1024||port>65535)throw Error('Port must be 1024..65535');
 const data=startup({root:path.join(__dirname,id),manifest,bootstrap:bootstrap.file,saveName:bootstrap.saveName,arity:bootstrap.arity,runRoot:path.join(root,'data/sessions'),persistent:true,readyPatterns:['IZ launcher runtime is running\\.']});
 Object.assign(data.bootstrap,bootstrap);
 const env={IZ15_LOADER:id,IZ15_LOADER_SETTINGS:JSON.stringify(settings),HB_PORT:String(port),HB_MANUAL_INPUT:'1',HB_INPUT:'manual',HB_RESUME:bootstrap.recovery?'1':'0'};
 if(options.appsRoot)env.HB_APPS_DIR=path.resolve(options.appsRoot);
 return {loaderId:id,mode:'izlauncher',runtime:'izlauncher',runner:'izlauncher',command:node,args:[options.izEntrypoint||'core/session/console-runner.cjs',host],cwd:root,env,startup:data,bootstrap:data.bootstrap,manualCommands:data.manualCommands,runRoot:data.runRoot,lifecycle:data.lifecycle,capabilities:{...manifest.capabilities,sharedIzRuntime:true,izPackages:true,streaming:true},notes:['The selected acquisition transport is qualified before the common IZlauncher runtime starts. Original upstream launch remains a separate target.']};
}
module.exports={startup,base,inputSteps,connectSteps,izPlan,MANUAL_COMMANDS};
