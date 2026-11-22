// Compatibility entry for the current IZ2s shell loop.
const implementation=require('../../shells/izlauncher/server/frame-loop.cjs');
module.exports=implementation;
if(require.main===module)implementation.main(...process.argv.slice(2)).catch(error=>{console.error(error);process.exitCode=1;});
