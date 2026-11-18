// The first 340 bytes keep the existing controller ABI; the tail is HID touch.
const BYTES=396,TOUCH=340;
function create(send,{width=1280,height=720,now=Date.now,size=()=>({width,height})}={}){
 const choose=require('./controller-source.cjs').create(),gate=require(require('node:path').join((require('./platform.cjs').root),"core/services/stream-controls.cjs")).createGate(),decode=require(require('node:path').join((require('./platform.cjs').root),"core/services/controller-packet.cjs")).decode;
 let held=false,last=null,buttons=0,lastKeyAt=0;
 const keys=[[1,'Return'],[2,'Escape'],[4,'Space'],[4096,'Left'],[8192,'Up'],[16384,'Right'],[32768,'Down']];
 function release(){if(held)send({type:'touch',phase:'touchCancel'});held=false;last=null;for(const [mask,key]of keys)if(buttons&mask)send({type:'key',phase:'keyUp',key});buttons=0;}
 function input(packet){
  if(packet.length<340)return release();const state=gate(decode(choose(packet).packet)),active=state.mode===2&&state.menu===0;
  if(!active||state.inputBlocked)return release();
  if(!(packet.readUInt32LE(332)&2))state.buttons=0;else{const [x,y]=state.axes;if(x>12000)state.buttons|=16384;if(x< -12000)state.buttons|=4096;if(y>12000)state.buttons|=8192;if(y< -12000)state.buttons|=32768;}
  for(const [mask,key]of keys){if((state.buttons&mask)&&!(buttons&mask)){send({type:'key',phase:'keyDown',key});lastKeyAt=now();}else if(!(state.buttons&mask)&&(buttons&mask))send({type:'key',phase:'keyUp',key});else if((state.buttons&mask)&&now()-lastKeyAt>=160){send({type:'key',phase:'keyDown',key});lastKeyAt=now();}}
  buttons=state.buttons;
  if(packet.length<BYTES)return;
  const count=packet.readUInt32LE(TOUCH+8),cancel=(packet.readUInt32LE(TOUCH+24)&2)!==0;
  if(count!==1||cancel){if(held)send({type:'touch',phase:cancel||count>1?'touchCancel':'touchEnd'});held=false;last=null;return;}
  const {width,height}=size();const x=Math.round(Math.max(0,Math.min(1279,packet.readUInt32LE(TOUCH+32)))*width/1280),y=Math.round(Math.max(0,Math.min(719,packet.readUInt32LE(TOUCH+36)))*height/720),point=x+':'+y;
  if(!held||point!==last)send({type:'touch',phase:held?'touchMove':'touchStart',x,y});held=true;last=point;
 }
 return {input,release};
}
module.exports={BYTES,TOUCH,create};
