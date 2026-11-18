const hook=require('./streaming/session-hook.cjs'),engine=require('./streaming/stream.cjs'),sizes=require('./local-package/render-sizes.cjs');
async function create(context,options={}){
 const settings=await require('./render-settings.cjs').open();
 async function publish(){const value=Buffer.alloc(8);value.writeUInt32LE(settings.profile().height);value.writeUInt32LE(engine.ENGINES.indexOf(settings.engine()),4);await context.rpc.write(context.mapped+0x700c80+344,value);}
 await publish();let last=0,work=Promise.resolve(),activeHost;
 const create=options.createHost||((value)=>require('./streaming/application-host.cjs').create({...value,panelLabel:'VacooTube',streamFactory:args=>engine.uploader(args,{engine:settings.engine()}),playerIntegration:{create:directory=>require('./remote-youtube.cjs').create(directory,{profile:settings.profile()}),package:require('./local-package/index.cjs'),stream:{start:args=>engine.start({...args,profileFor:settings.profile},{engine:settings.engine()})}}}));
 const host=await hook.create(context,{...options,createHost:async value=>(activeHost=await create(value))});
 return {...host,onState(state){const action=state.readFloatLE(968),seq=state.readFloatLE(972),resolution=Number.isInteger(action)&&action>=48&&action<48+sizes.HEIGHTS.length,render=action===56||action===57;
  if((resolution||render)&&Number.isInteger(seq)&&seq>0){if(seq!==last){last=seq;work=work.then(async()=>{
   await host.drain();
   if(resolution)await settings.set(sizes.HEIGHTS[action-48]);
   else {const next=engine.ENGINES[action-56];if(next!==settings.engine()){if(activeHost)await activeHost.replaceRenderEngine(()=>settings.setEngine(next));else await settings.setEngine(next);}}
   await publish();
  }).catch(async error=>{console.error('VacooTube render settings: '+error.message);await publish().catch(()=>{});});}return work;}
  return host.onState(state);
 },async suspend(){await work;await host.suspend();activeHost=null;},async close(){await work;await host.close();activeHost=null;}};
}
module.exports={create};
