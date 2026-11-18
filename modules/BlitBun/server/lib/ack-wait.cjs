// A missing ACK may mean HOME suspended the app. Keep the same packet in flight.
function deadline({onPause,onTimeout,softMs=2000,limitMs=120000}){
 let closed=false;
 const soft=setTimeout(()=>{if(!closed)onPause();},softMs);
 const hard=setTimeout(()=>{if(!closed)onTimeout();},limitMs);
 return {clear(){if(closed)return;closed=true;clearTimeout(soft);clearTimeout(hard);}};
}
module.exports={deadline};
