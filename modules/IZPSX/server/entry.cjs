'use strict';
// The optional PC entry hands off to console-owned native execution. The SD
// application carries its own native code and never loads this entry.
module.exports={load(name){if(name!=='launch')throw Error('Unknown IZPSX component');return require('./launch.cjs');},async createModule(){return {async init(){},async start(){},async stop(){},async dispose(){}};}};
