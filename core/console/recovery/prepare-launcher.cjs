'use strict';
// Ordinary PC-backed launchers need the same foreground protection as SD.
// Never start graphics after an unqualified/uncertain recovery handover.
async function main(directory){
 const proof=await require('./prepare-sd.cjs').prepare({directory});
 await require('../../session/ledger.cjs').saveJson(require('node:path').join(directory,'launcher-foreground-ready.json'),{...proof,format:'iz-launcher-foreground-ready-v1'});
 console.log('Launcher BASIC foreground parked; native connection recovery ready.');
 return proof;
}
module.exports={main};if(require.main===module)main(process.argv[2]).catch(error=>{console.error(error);process.exitCode=1;});
