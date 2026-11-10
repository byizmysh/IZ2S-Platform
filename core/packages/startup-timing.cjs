'use strict';
// One record per startup phase. No RPC and no polling-loop instrumentation.
function create({report,clock=()=>performance.now(),log=console.log}){
 const started=clock(),state=report.startupTiming={phases:[],totalMs:null};let active=null,at=started;
 function close(now){if(active){const row={name:active,milliseconds:now-at};state.phases.push(row);log('[startup] '+row.name+': '+row.milliseconds.toFixed(1)+' ms');}}
 return {phase(name){const now=clock();close(now);active=name;at=now;log('[startup] Starting '+name);},finish(){const now=clock();close(now);active=null;state.totalMs=now-started;}};
}
module.exports={create};
