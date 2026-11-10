// Scheduling and transfers are shared; this extension owns its presentation.
const manager=require('../../../core/packages/menu-manager.cjs');
module.exports={create:options=>manager.create(options,{id:'izpslauncher',name:'IZPSlauncher',serverDirectory:__dirname,extraMenuData:()=>[{offset:0x50000,bytes:require('./clock.cjs').header()}],createActivity:options=>require('./activity.cjs').create(options)})};
