'use strict';
// Local production menu runs; the fixture replaces console RPC with bounded
// memory. Run each shell in its own process so validation caches start cold.
const fs=require('node:fs/promises'),path=require('node:path'),cp=require('node:child_process'),util=require('node:util');
const root=path.resolve(__dirname,'..');
async function worker(shell){
 if(!['izlauncher','izpslauncher'].includes(shell))throw Error('Unknown launcher');
 const original=cp.execFile,run=util.promisify(original),counts={shader:0,image:0,render:0};
 const count=(command,args)=>{if(String(command).includes('spirv-val'))counts.shader++;else if(args?.[0]==='-c'&&args?.[1]?.includes('PIL'))counts.image++;else counts.render++;};
 function traced(command,args,...rest){count(command,args);return original.call(this,command,args,...rest);}
 traced[util.promisify.custom]=function(command,args,...rest){count(command,args);return run(command,args,...rest);};cp.execFile=traced;
 try{const at=performance.now(),row=await require('../tests/launcher-performance-fixture.cjs').run(shell);return {...row,wholeFixtureMs:performance.now()-at,validatorProcesses:counts};}
 finally{cp.execFile=original;}
}
async function main(label='final'){
 if(!/^[a-z0-9-]+$/.test(label))throw Error('Invalid audit label');
 const rows=[],run=util.promisify(cp.execFile);
 for(const shell of ['izlauncher','izpslauncher']){const result=await run(process.execPath,[__filename,'--worker',shell],{cwd:root,maxBuffer:131072});rows.push(JSON.parse(result.stdout));}
 const file=path.join(root,'data/diagnostics/launcher-optimization-round2-'+label+'.json');await fs.mkdir(path.dirname(file),{recursive:true});await fs.writeFile(file,JSON.stringify({at:new Date().toISOString(),coldProcessPerShell:true,rows},null,2)+'\n');
 for(const row of rows)console.log(JSON.stringify({shell:row.shell,validators:row.validatorProcesses,bootMs:row.boot.elapsedMs,returnMs:row.returned.elapsedMs,fixtureMs:row.wholeFixtureMs,menuSha256:row.menuSha256}));
 return rows;
}
module.exports={main};
if(require.main===module)(process.argv[2]==='--worker'?worker(process.argv[3]).then(row=>console.log(JSON.stringify(row))):main(process.argv[2])).catch(error=>{console.error(error);process.exitCode=1;});
