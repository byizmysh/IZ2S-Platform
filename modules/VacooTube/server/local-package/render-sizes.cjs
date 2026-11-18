const HEIGHTS=Object.freeze([1080,720,540,480,360,240]);
function profile(height=720){height=Number(height);if(!HEIGHTS.includes(height))throw Error('Unsupported VacooTube render resolution');return {mode:'vacootube',width:Math.round(height*16/9/4)*4,height,fps:0};}
module.exports={HEIGHTS,profile};
