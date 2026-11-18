const path=require('node:path'),{execFile}=require('node:child_process');
function match(windows,width,height,label=''){
 const full={x:0,y:0,w:width,h:height,exact:false};if(!Number.isFinite(width)||!Number.isFinite(height)||width<4||height<4)return full;
 const candidates=[];for(const v of windows){const f=v.frame,c=v.client;if(!f||!c||f.w<=0||f.h<=0||c.w<=0||c.h<=0)continue;const sx=width/f.w,sy=height/f.h;if(Math.abs(sx-sy)/Math.max(sx,sy)>.005)continue;
  const left=Math.round((c.x-f.x)*sx),top=Math.round((c.y-f.y)*sy),w=Math.round(c.w*sx),h=Math.round(c.h*sy);if(left<0||top<0||w<4||h<4||left+w>width||top+h>height)continue;
  const title=(v.title||'').toLowerCase(),score=label.toLowerCase().includes(title)&&title?10:0;candidates.push({x:left,y:top,w,h,exact:true,score,windowId:score===10&&typeof v.id==='string'?v.id:undefined});
 }
 if(!candidates.length)return full;candidates.sort((a,b)=>b.score-a.score);const best=candidates.filter(v=>v.score===candidates[0].score),same=best.every(v=>['x','y','w','h'].every(k=>v[k]===best[0][k]));if(!same)return full;const {score,windowId,...result}=best[0];if(windowId&&best.every(v=>v.windowId===windowId))result.windowId=windowId;return result;
}
let cached,at=0;
async function crop(width,height,label){
 if(!cached||Date.now()-at>1000){cached=await new Promise(resolve=>execFile('python3',[path.join(__dirname,'remote/window-frames.py')],{timeout:2500,maxBuffer:128*1024},(e,out)=>{try{resolve(e?[]:JSON.parse(out));}catch{resolve([]);}}));at=Date.now();}
 return match(cached,width,height,label);
}
module.exports={match,crop};
