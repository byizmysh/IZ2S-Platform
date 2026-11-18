// Scheduling and transfers are shared; this extension owns its presentation.
const manager=require('../../../core/packages/menu-manager.cjs');
module.exports={create:options=>manager.create(options,{id:'izlauncher',name:'IZ2S',serverDirectory:__dirname,refreshOnPickerClose:options.refreshHomebrew===true,createActivity:options=>require('./activity.cjs').create(options)})};
