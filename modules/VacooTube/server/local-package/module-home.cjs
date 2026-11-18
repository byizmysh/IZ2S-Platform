const path=require('node:path');
// A verified .versions snapshot shares its physical module's runtime and data.
const marker=path.sep+'modules'+path.sep,index=__dirname.lastIndexOf(marker);
const end=index<0?-1:__dirname.indexOf(path.sep,index+marker.length);
module.exports=end>=0?__dirname.slice(0,end):path.resolve(__dirname,'../..');
