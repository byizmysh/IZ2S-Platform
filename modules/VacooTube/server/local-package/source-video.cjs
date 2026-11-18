// Source video quality, independent of the 1280x720 streaming surface.
const DEFAULT_QUALITY='large',DEFAULT_HEIGHT=480;
function installQualityDefault(){
 if(window.__izSourceVideoQuality)return;window.__izSourceVideoQuality=true;
 try{const now=Date.now();localStorage.setItem('yt-player-quality',JSON.stringify({data:JSON.stringify({quality:480,previousQuality:480}),expiration:now+31536000000,creation:now}));}catch{}
 const desired='large',height={tiny:144,small:240,medium:360,large:480,hd720:720,hd1080:1080,highres:2160},applied=new WeakMap();
 function apply(){
  let ready=false;
  for(const player of document.querySelectorAll('.html5-video-player')){
   const id=player.getVideoData?.()?.video_id,levels=player.getAvailableQualityLevels?.();
   if(!id||!Array.isArray(levels)||!levels.length)continue;
   if(applied.get(player)===id){ready=true;continue;}
   const quality=levels.includes(desired)?desired:levels.filter(q=>height[q]&&height[q]<=480).sort((a,b)=>height[b]-height[a])[0];
   if(!quality)continue;
   if(typeof player.setPlaybackQualityRange==='function')player.setPlaybackQualityRange(quality,quality);
   else if(typeof player.setPlaybackQuality==='function')player.setPlaybackQuality(quality);
   else continue;
   applied.set(player,id);ready=true;
  }
  return ready;
 }
 // Apply once per video so an explicit quality choice can still take effect.
 let timer;function schedule(){if(timer)clearInterval(timer);if(apply())return;let tries=0;timer=setInterval(()=>{if(apply()||++tries>=60){clearInterval(timer);timer=null;}},1000);}
 document.addEventListener('loadedmetadata',schedule,true);
 document.addEventListener('yt-navigate-finish',schedule,true);schedule();
}
const pageScript='('+installQualityDefault.toString()+')();';
module.exports={DEFAULT_QUALITY,DEFAULT_HEIGHT,pageScript};
