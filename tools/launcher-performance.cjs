'use strict';
const fs=require('node:fs/promises'),path=require('node:path');
async function main(label){if(!/^[a-z0-9-]+$/.test(label||''))throw Error('Choose a benchmark label');const rows=[];for(const shell of ['izlauncher','izpslauncher']){const row=await require('../tests/launcher-performance-fixture.cjs').run(shell);rows.push(row);console.log(JSON.stringify(row));}const file=path.resolve(__dirname,'../data/diagnostics/launcher-performance-'+label+'.json');await fs.writeFile(file,JSON.stringify({at:new Date().toISOString(),rows},null,2)+'\n');return rows;}
module.exports={main};if(require.main===module)main(process.argv[2]).catch(e=>{console.error(e);process.exitCode=1;});
