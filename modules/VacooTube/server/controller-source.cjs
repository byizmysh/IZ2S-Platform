// Joy-Con halves form one controller; a Pro Controller is a second source.
// Last meaningful input selects the source, while a neutral wake-up keeps play.
function read(packet,indices){let buttons=0,axes=[0,0,0,0],connected=false;
 for(const i of indices){const p=8+i*64;if(!(packet.readUInt32LE(p+32)&1))continue;connected=true;buttons|=packet.readUInt32LE(p+8);for(let a=0;a<4;a++){const n=packet.readInt32LE(p+16+a*4);if(Math.abs(n)>Math.abs(axes[a]))axes[a]=n;}}
 const active=!!buttons||axes.some(n=>Math.abs(n)>8192),signature=[buttons,...axes.map(n=>Math.abs(n)<=8192?0:Math.round(n/2048))].join(':');return {connected,active,signature};
}
function create({selection=()=>({source:'auto',controllerId:null})}={}){let active=null,previous={},selectionStamp;return input=>{
 const preference=selection()||{},stamp=JSON.stringify([preference.source,preference.controllerId]),selectionChanged=selectionStamp!==undefined&&selectionStamp!==stamp;selectionStamp=stamp;
 const packet=Buffer.from(input);
 // SDK selectors supply the chosen player for each style; handheld remains a
 // separate source and must not merge with a specifically selected Joy-Con.
 if(preference.controllerId!==null&&preference.controllerId!==undefined){
  if(preference.controllerId===32)for(const i of [1,2,3,4])packet.fill(0,8+i*64,8+(i+1)*64);
  else packet.fill(0,8,72);
 }
 const groups={joycon:read(packet,[0,2,3,4]),pro:read(packet,[1])},before=active;
 if(selectionChanged)active=null;
 if(!groups[active]?.connected)active=null;
 const forced=['pro','joycon'].includes(preference.source)?preference.source:null;
 if(forced)active=groups[forced].connected?forced:null;
 else{for(const name of ['joycon','pro'])if(groups[name].connected&&groups[name].active&&groups[name].signature!==previous[name])active=name;
 if(!active)active=groups.pro.connected?'pro':groups.joycon.connected?'joycon':null;}
 previous=Object.fromEntries(Object.entries(groups).map(([name,value])=>[name,value.signature]));
 const selected=Buffer.from(packet);for(const i of (active==='pro'?[0,2,3,4]:active==='joycon'?[1]:[0,1,2,3,4]))selected.fill(0,8+i*64,8+(i+1)*64);
 return {packet:selected,source:active==='pro'?'Pro Controller':active==='joycon'?'Joy-Con':null,controllerId:preference.controllerId??null,changed:selectionChanged||before!==active};
 };}
module.exports={create};
