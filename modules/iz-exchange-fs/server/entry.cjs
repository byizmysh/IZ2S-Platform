module.exports=require(require('node:path').join(require('./platform.cjs').root,'core/module-adapter.cjs')).create('iz-exchange-fs',require('../module.json').server.implementations,__dirname);
