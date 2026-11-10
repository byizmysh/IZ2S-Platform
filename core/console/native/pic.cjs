'use strict';
const {execFileSync}=require('node:child_process');
// Inspect relocatable objects, BEFORE static linking erases relocation records.
// A final ELF with no relocations can still contain wrong zero-based pointers.
function verifyObject(object){
 const relocations=execFileSync('llvm-readelf',['--wide','-r',object],{encoding:'utf8'}),bad=relocations.split('\n').filter(line=>/R_AARCH64_(?:ABS(?:64|32|16)|MOVW_[US]ABS|.*GOT)/.test(line));
 if(bad.length)throw Error('Raw native image contains absolute or GOT pointers; construct callback tables at runtime: '+bad.map(line=>line.trim()).join('; '));return true;
}
module.exports={verifyObject};
