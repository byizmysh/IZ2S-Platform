'use strict';
const assert=require('node:assert/strict');
function wrap(command){
 assert.match(command,/^V=START\([0-9,]+\)$/,'Expected the original verified START command');
 const sleep=require('../console/abi/thread-code.json').functions.sleep;
 assert.equal(sleep.offset,0x129500,'Requalify BASIC foreground sleep');
 assert.equal(sleep.bytes,'e86b1214','Requalify BASIC foreground sleep');
 return command+':WHILE 1:R#=CALL#(&H129500,1000000000,0,0,0):WEND';
}
module.exports={wrap};
