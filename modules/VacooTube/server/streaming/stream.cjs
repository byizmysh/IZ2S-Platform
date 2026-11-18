const {ownTransport}=require('./render-lifecycle.cjs');
const ENGINES=['run13','run16'];
function selected(value='run13'){if(!ENGINES.includes(value))throw Error('Unsupported render engine: '+value);return value;}
function library(engine,name){return require((selected(engine)==='run16'?'./run16/lib/':'./lib/')+name);}
function start(options,{engine='run13'}={}){selected(engine);Object.assign(options.stream.stats,{streamingEngine:engine==='run13'?'Classic':'Advanced',scheduler:engine==='run13'?'Classic':'Advanced'});return library(engine,'hl2-stream.cjs').start(options);}
function uploader(options,{engine='run13'}={}){return ownTransport(library(engine,'pc-stream-delta.cjs').uploader(options),{heartbeat:engine==='run13'?options.onHeartbeat:undefined});}
module.exports={start,uploader,selected,ENGINES};
