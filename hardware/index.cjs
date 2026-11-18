const fs=require('node:fs'),path=require('node:path');
const ID=/^[a-z0-9][a-z0-9-]{0,47}$/;
const metadataCache=new Map();
function catalog(root){const devices=[],errors=[],folder=path.join(root,'hardware/devices');if(!fs.existsSync(folder))return {devices,errors};
 for(const e of fs.readdirSync(folder,{withFileTypes:true})){if(!e.isDirectory()||!ID.test(e.name))continue;try{
 const file=path.join(folder,e.name,'device.json');if(fs.statSync(file).size>65536)throw Error('Device descriptor too large');const stat=fs.statSync(file),old=metadataCache.get(file);const d=old&&old.stamp===stat.mtimeMs+':'+stat.size?old.value:JSON.parse(fs.readFileSync(file));metadataCache.set(file,{stamp:stat.mtimeMs+':'+stat.size,value:d});
 if(d.format!=='izlauncher.device'||d.apiVersion!==1||d.id!==e.name||typeof d.name!=='string'||!Array.isArray(d.roles)||!d.roles.length||d.roles.some(x=>!['server','input','console'].includes(x))||!Array.isArray(d.fields))throw Error('Invalid device descriptor');
 if(d.validator){require('../core/descriptors.cjs').relative(d.validator);if(!fs.statSync(path.join(path.dirname(file),d.validator)).isFile())throw Error('Missing device settings validator');}
 devices.push({...d,folder:path.dirname(file)});
 }catch(e){errors.push({id:e.name,error:e.message});}}
 return {devices:devices.sort((a,b)=>a.name.localeCompare(b.name)),errors};}
function compose(root,definition,inventory=catalog(root)){if(!definition.nodes)return definition;
 const nodes=definition.nodes.map(node=>{const device=inventory.devices.find(d=>d.id===node.device);if(!device)throw Error('Missing hardware module: '+node.device);if(node.roles.some(r=>!device.roles.includes(r)))throw Error('Device cannot fulfil selected role');return {...node,definition:device};});
 for(const role of ['server','input','console'])if(nodes.filter(n=>n.roles.includes(role)).length!==1)throw Error('Configuration requires exactly one '+role+' role');
 const server=nodes.find(n=>n.roles.includes('server')),input=nodes.find(n=>n.roles.includes('input')),consoleNode=nodes.find(n=>n.roles.includes('console'));
 for(const link of definition.links||[]){if(!nodes.some(n=>n.id===link.from)||!nodes.some(n=>n.id===link.to))throw Error('Hardware link has an unknown endpoint');}
 if(nodes.length<2||nodes.length>3||new Set(nodes.map(n=>n.id)).size!==nodes.length)throw Error('Choose two or three distinct hardware nodes');
 const fields=[...new Map(nodes.flatMap(n=>n.definition.fields).map(f=>[f.key,f])).values()];
 const combined=server.id===input.id,checks=[{id:'server',label:combined?server.definition.name+' · server + input':server.definition.name+' · server',icon:'i-server'},...(!combined?[{id:'arm',label:input.definition.name+' · input',icon:'i-arm'}]:[]),{id:'usb',label:'USB → '+consoleNode.definition.name,icon:'i-usb'},{id:'lan',label:consoleNode.definition.name,icon:'i-switch'}];
 return {...definition,nodes,fields,ui:{...definition.ui,checks:definition.ui?.checks||checks},inputAdapter:input.definition.transport.adapter,executionHost:definition.executionHost||(server.id===input.id?'arm':'pc'),hardware:{deviceCount:nodes.length,combined:server.id===input.id,server:server.definition,input:input.definition,console:consoleNode.definition,links:definition.links||[],errors:inventory.errors}};
}
function present(configuration,result){const ui=configuration.ui;if(!ui?.checks)return result;
 const source=new Map((result.items||[]).map(x=>[x.id,x]));
 const items=ui.checks.map(row=>{let item={id:row.id,ready:false,detail:'Not checked',...source.get(row.id),label:row.label,icon:row.icon};if(configuration.hardware?.combined&&row.id==='server'){const input=source.get('arm');if(input){item.ready=item.ready&&input.ready;item.detail=item.detail.replace(/\.$/,'')+'. Input: '+input.detail;}}return item;});
 return {...result,items,ready:!result.setupRequired&&items.every(x=>x.ready||x.pending)};
}
function configurations(root,hardwareRoot=root){const inventory=catalog(hardwareRoot),scan=require('../core/descriptors.cjs').scan(root,'profile'),items=[],errors=[...scan.errors,...inventory.errors];for(const d of scan.items){try{items.push(compose(hardwareRoot,d,inventory));}catch(e){errors.push({id:d.id,error:e.message});}}return {items,errors};}
module.exports={catalog,compose,present,configurations};
