'use strict';
// Presentation categories are separate from the built-in launch scene ABI.
// Compatibility entries cover existing packages which predate manifest.category.
const values={game:1,tool:2,media:3};
function classify(app){
 const m=app.manifest||app;
 if(m.category!==undefined){if(!Object.hasOwn(values,m.category))throw Error('Unknown application category');return values[m.category];}
 if([4,5,8].includes(m.shellContribution?.scene))return values.game;
 const id=app.id||m.id||'';
 if(id==='izdoom-prime18'||/^ps2-cold-/.test(id))return values.game;
 return values.tool;
}
module.exports={classify,values,OFFSET:102508};
